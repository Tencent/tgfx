/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "CornerPinEffect.h"
#include <string>
#include "core/utils/UniqueID.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {
static constexpr size_t CORNER_PIN_VERTEX_SIZE = 20 * sizeof(float);

static constexpr char CORNER_PIN_VERTEX_SHADER[] = R"(
        in vec2 aPosition;
        in vec3 aTextureCoord;
        out vec3 vertexColor;
        void main() {
            vec3 position = vec3(aPosition, 1);
            gl_Position = vec4(position.xy, 0, 1);
            vertexColor = aTextureCoord;
        }
    )";

static constexpr char CORNER_PIN_FRAGMENT_SHADER[] = R"(
        precision mediump float;
        in vec3 vertexColor;
        uniform sampler2D sTexture;
        out vec4 tgfx_FragColor;
        void main() {
            tgfx_FragColor = texture(sTexture, vertexColor.xy / vertexColor.z);
        }
    )";

static std::string GetFinalShaderCode(const char* codeSnippet, bool isDesktop) {
  if (isDesktop) {
    return std::string("#version 150\n\n") + codeSnippet;
  }
  return std::string("#version 300 es\n\n") + codeSnippet;
}

std::shared_ptr<CornerPinEffect> CornerPinEffect::Make(EffectCache* cache, const Point& upperLeft,
                                                       const Point& upperRight,
                                                       const Point& lowerRight,
                                                       const Point& lowerLeft) {
  return std::make_shared<CornerPinEffect>(cache, upperLeft, upperRight, lowerRight, lowerLeft);
}

CornerPinEffect::CornerPinEffect(EffectCache* cache, const Point& upperLeft,
                                 const Point& upperRight, const Point& lowerRight,
                                 const Point& lowerLeft)
    : cache(cache) {
  cornerPoints[0] = lowerLeft;
  cornerPoints[1] = lowerRight;
  cornerPoints[2] = upperLeft;
  cornerPoints[3] = upperRight;
  calculateVertexQs();
}

Rect CornerPinEffect::filterBounds(const Rect&, MapDirection mapDirection) const {
  if (mapDirection == MapDirection::Reverse) {
    const auto largeSize = static_cast<float>(1 << 29);
    return Rect::MakeLTRB(-largeSize, -largeSize, largeSize, largeSize);
  }
  auto& lowerLeft = cornerPoints[0];
  auto& lowerRight = cornerPoints[1];
  auto& upperLeft = cornerPoints[2];
  auto& upperRight = cornerPoints[3];
  auto left = std::min(std::min(upperLeft.x, lowerLeft.x), std::min(upperRight.x, lowerRight.x));
  auto top = std::min(std::min(upperLeft.y, lowerLeft.y), std::min(upperRight.y, lowerRight.y));
  auto right = std::max(std::max(upperLeft.x, lowerLeft.x), std::max(upperRight.x, lowerRight.x));
  auto bottom = std::max(std::max(upperLeft.y, lowerLeft.y), std::max(upperRight.y, lowerRight.y));
  return Rect::MakeLTRB(left, top, right, bottom);
}

std::shared_ptr<RenderPipeline> CornerPinEffect::createPipeline(GPU* gpu) const {
  auto info = gpu->info();
  auto isDesktop = info->version.find("OpenGL ES") == std::string::npos;
  ShaderModuleDescriptor vertexModule = {};
  vertexModule.code = GetFinalShaderCode(CORNER_PIN_VERTEX_SHADER, isDesktop);
  vertexModule.stage = ShaderStage::Vertex;
  auto vertexShader = gpu->createShaderModule(vertexModule);
  if (vertexShader == nullptr) {
    return nullptr;
  }
  ShaderModuleDescriptor fragmentModule = {};
  fragmentModule.code = GetFinalShaderCode(CORNER_PIN_FRAGMENT_SHADER, isDesktop);
  fragmentModule.stage = ShaderStage::Fragment;
  auto fragmentShader = gpu->createShaderModule(fragmentModule);
  if (fragmentShader == nullptr) {
    return nullptr;
  }
  RenderPipelineDescriptor descriptor = {};
  VertexBufferLayout vertexLayout(
      {{"aPosition", VertexFormat::Float2}, {"aTextureCoord", VertexFormat::Float3}});
  descriptor.vertex.bufferLayouts = {vertexLayout};
  descriptor.vertex.module = vertexShader;
  descriptor.fragment.module = fragmentShader;
  descriptor.fragment.colorAttachments.push_back({});
  BindingEntry textureBinding = {"sTexture", 0};
  descriptor.layout.textureSamplers.push_back(textureBinding);
  return gpu->createRenderPipeline(descriptor);
}

bool CornerPinEffect::onDraw(CommandEncoder* encoder,
                             const std::vector<std::shared_ptr<Texture>>& inputTextures,
                             std::shared_ptr<Texture> outputTexture, const Point& offset) const {
  auto gpu = encoder->gpu();
  static const uint32_t CornerPinEffectType = UniqueID::Next();
  auto pipeline = cache->findPipeline(CornerPinEffectType);
  if (pipeline == nullptr) {
    pipeline = createPipeline(gpu);
    if (pipeline == nullptr) {
      return false;
    }
    cache->addPipeline(CornerPinEffectType, pipeline);
  }

  // Ideally, this MSAA texture should be cached and reused to avoid impacting performance.
  // However, since CornerPinEffect is only used for testing, we create it each time for simplicity.
  TextureDescriptor textureDesc(outputTexture->width(), outputTexture->height(),
                                outputTexture->format(), false, 4, TextureUsage::RENDER_ATTACHMENT);
  auto renderTexture = gpu->createTexture(textureDesc);
  if (renderTexture == nullptr) {
    return false;
  }
  RenderPassDescriptor renderPassDesc(renderTexture, LoadAction::Clear, StoreAction::Store,
                                      PMColor::Transparent(), outputTexture);
  auto renderPass = encoder->beginRenderPass(renderPassDesc);
  if (renderPass == nullptr) {
    return false;
  }
  renderPass->setPipeline(std::move(pipeline));
  auto vertexBuffer = gpu->createBuffer(CORNER_PIN_VERTEX_SIZE, GPUBufferUsage::VERTEX);
  if (vertexBuffer == nullptr) {
    return false;
  }
  auto vertices = static_cast<float*>(vertexBuffer->map());
  if (vertices == nullptr) {
    return false;
  }
  auto& inputTexture = inputTextures[0];
  collectVertices(inputTexture.get(), outputTexture.get(), offset, vertices);
  vertexBuffer->unmap();
  renderPass->setVertexBuffer(0, vertexBuffer);
  SamplerDescriptor samplerDesc(AddressMode::ClampToEdge, AddressMode::ClampToEdge,
                                FilterMode::Linear, FilterMode::Linear, MipmapMode::None);
  auto sampler = gpu->createSampler(samplerDesc);
  renderPass->setTexture(0, inputTexture, sampler);
  renderPass->draw(PrimitiveType::TriangleStrip, 4);
  renderPass->end();
  return true;
}

static Point ToGLVertexPoint(const Point& point, const Texture* target) {
  return {2.0f * point.x / static_cast<float>(target->width()) - 1.0f,
          2.0f * point.y / static_cast<float>(target->height()) - 1.0f};
}

static Point ToGLTexturePoint(const Point& point, const Texture* source) {
  return {point.x / static_cast<float>(source->width()),
          point.y / static_cast<float>(source->height())};
}

void CornerPinEffect::collectVertices(const Texture* source, const Texture* target,
                                      const Point& offset, float* vertices) const {
  size_t index = 0;
  auto textureWidth = static_cast<float>(source->width());
  auto textureHeight = static_cast<float>(source->height());
  Point texturePoints[4] = {{0.0f, textureHeight},
                            {textureWidth, textureHeight},
                            {0.0f, 0.0f},
                            {textureWidth, 0.0f}};
  for (size_t i = 0; i < 4; i++) {
    auto vertexPoint = ToGLVertexPoint(cornerPoints[i] + offset, target);
    vertices[index++] = vertexPoint.x;
    vertices[index++] = vertexPoint.y;
    auto texturePoint = ToGLTexturePoint(texturePoints[i], source);
    vertices[index++] = texturePoint.x * vertexQs[i];
    vertices[index++] = texturePoint.y * vertexQs[i];
    vertices[index++] = vertexQs[i];
  }
}

static float calculateDistance(const Point& intersection, const Point& vertexPoint) {
  return std::sqrtf(std::powf(fabsf(intersection.x - vertexPoint.x), 2) +
                    std::powf(fabsf(intersection.y - vertexPoint.y), 2));
}

static bool PointIsBetween(const tgfx::Point& point, const tgfx::Point& start,
                           const tgfx::Point& end) {
  auto minX = std::min(start.x, end.x);
  auto maxX = std::max(start.x, end.x);
  auto minY = std::min(start.y, end.y);
  auto maxY = std::max(start.y, end.y);
  return minX <= point.x && point.x <= maxX && minY <= point.y && point.y <= maxY;
}

void CornerPinEffect::calculateVertexQs() {
  // https://www.reedbeta.com/blog/quadrilateral-interpolation-part-1/
  // compute the intersection of the two diagonals: y1 = k1 * x1 + b1; y2 = k2 * x2 + b2
  auto& lowerLeft = cornerPoints[0];
  auto& lowerRight = cornerPoints[1];
  auto& upperLeft = cornerPoints[2];
  auto& upperRight = cornerPoints[3];
  auto ll2ur_k = (upperRight.y - lowerLeft.y) / (upperRight.x - lowerLeft.x);
  auto ul2lr_k = (lowerRight.y - upperLeft.y) / (lowerRight.x - upperLeft.x);
  auto ll2ur_b = lowerLeft.y - ll2ur_k * lowerLeft.x;
  auto ul2lr_b = upperLeft.y - ul2lr_k * upperLeft.x;
  tgfx::Point intersection = {0, 0};
  intersection.x = (ul2lr_b - ll2ur_b) / (ll2ur_k - ul2lr_k);
  intersection.y = ll2ur_k * intersection.x + ll2ur_b;
  // compute the distance between the intersection and the 4 vertices
  auto lowerLeftDistance = calculateDistance(intersection, lowerLeft);
  auto lowerRightDistance = calculateDistance(intersection, lowerRight);
  auto upperRightDistance = calculateDistance(intersection, upperRight);
  auto upperLeftDistance = calculateDistance(intersection, upperLeft);
  // compute the uvq of the 4 vertices : uvq0 = float3(u0, v0, 1) * (d0 + d2) / d2
  if (PointIsBetween(intersection, lowerLeft, upperRight) &&
      PointIsBetween(intersection, upperLeft, lowerRight) && upperRightDistance != 0 &&
      upperLeftDistance != 0 && lowerRightDistance != 0 && lowerLeftDistance != 0) {
    vertexQs[0] = (lowerLeftDistance + upperRightDistance) / upperRightDistance;  // LowerLeft
    vertexQs[1] = (lowerRightDistance + upperLeftDistance) / upperLeftDistance;   // LowerRight
    vertexQs[2] = (upperLeftDistance + lowerRightDistance) / lowerRightDistance;  // UpperLeft
    vertexQs[3] = (upperRightDistance + lowerLeftDistance) / lowerLeftDistance;   // UpperRight
  } else {
    vertexQs[0] = 1.0f;
    vertexQs[1] = 1.0f;
    vertexQs[2] = 1.0f;
    vertexQs[3] = 1.0f;
  }
}
}  // namespace tgfx
