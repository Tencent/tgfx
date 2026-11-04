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
#include <memory>
#include "core/utils/UniqueID.h"
#include "tgfx/gpu/GPU.h"
#include "utils/NativeShader.h"

namespace tgfx {
static constexpr size_t CORNER_PIN_VERTEX_SIZE = 20 * sizeof(float);
static constexpr int MSAA_SAMPLE_COUNT = 4;

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

// Native variants of the same effect. Metal and D3D12 negate the clip-space y (the removed
// translation layer's flip); GL/Vulkan/WebGPU pass it through. The texture sampler binds at the
// first texture slot of every backend: [[texture(0)]]/[[sampler(0)]] on Metal, set 2 / binding 0
// on Vulkan, @group(2) texture binding 0 + sampler binding 1 on WebGPU, t0/s0 on D3D12.
static constexpr char CORNER_PIN_MSL_VERTEX_SHADER[] = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
  float2 aPosition [[attribute(0)]];
  float3 aTextureCoord [[attribute(1)]];
};

struct VertexOut {
  float4 position [[position]];
  float3 vertexColor;
};

vertex VertexOut main0(VertexIn in [[stage_in]]) {
  VertexOut out;
  out.position = float4(in.aPosition.x, -in.aPosition.y, 0.0, 1.0);
  out.vertexColor = in.aTextureCoord;
  return out;
}
)";

static constexpr char CORNER_PIN_MSL_FRAGMENT_SHADER[] = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexOut {
  float4 position [[position]];
  float3 vertexColor;
};

fragment float4 main0(VertexOut in [[stage_in]], texture2d<float> sTexture [[texture(0)]],
                      sampler sTextureSampler [[sampler(0)]]) {
  return sTexture.sample(sTextureSampler, in.vertexColor.xy / in.vertexColor.z);
}
)";

static const uint32_t kCornerPinVertexSpirv[] = {
    0x07230203, 0x00010300, 0x000d000b, 0x00000028, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0009000f, 0x00000000, 0x00000004, 0x6e69616d, 0x00000000, 0x0000000c, 0x00000018, 0x00000024,
    0x00000026, 0x00040047, 0x0000000c, 0x0000001e, 0x00000000, 0x00030047, 0x00000016, 0x00000002,
    0x00050048, 0x00000016, 0x00000000, 0x0000000b, 0x00000000, 0x00050048, 0x00000016, 0x00000001,
    0x0000000b, 0x00000001, 0x00050048, 0x00000016, 0x00000002, 0x0000000b, 0x00000003, 0x00050048,
    0x00000016, 0x00000003, 0x0000000b, 0x00000004, 0x00040047, 0x00000024, 0x0000001e, 0x00000000,
    0x00040047, 0x00000026, 0x0000001e, 0x00000001, 0x00020013, 0x00000002, 0x00030021, 0x00000003,
    0x00000002, 0x00030016, 0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000003,
    0x00040017, 0x0000000a, 0x00000006, 0x00000002, 0x00040020, 0x0000000b, 0x00000001, 0x0000000a,
    0x0004003b, 0x0000000b, 0x0000000c, 0x00000001, 0x0004002b, 0x00000006, 0x0000000e, 0x3f800000,
    0x00040017, 0x00000012, 0x00000006, 0x00000004, 0x00040015, 0x00000013, 0x00000020, 0x00000000,
    0x0004002b, 0x00000013, 0x00000014, 0x00000001, 0x0004001c, 0x00000015, 0x00000006, 0x00000014,
    0x0006001e, 0x00000016, 0x00000012, 0x00000006, 0x00000015, 0x00000015, 0x00040020, 0x00000017,
    0x00000003, 0x00000016, 0x0004003b, 0x00000017, 0x00000018, 0x00000003, 0x00040015, 0x00000019,
    0x00000020, 0x00000001, 0x0004002b, 0x00000019, 0x0000001a, 0x00000000, 0x0004002b, 0x00000006,
    0x0000001d, 0x00000000, 0x00040020, 0x00000021, 0x00000003, 0x00000012, 0x00040020, 0x00000023,
    0x00000003, 0x00000007, 0x0004003b, 0x00000023, 0x00000024, 0x00000003, 0x00040020, 0x00000025,
    0x00000001, 0x00000007, 0x0004003b, 0x00000025, 0x00000026, 0x00000001, 0x00050036, 0x00000002,
    0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003d, 0x0000000a, 0x0000000d,
    0x0000000c, 0x00050051, 0x00000006, 0x0000000f, 0x0000000d, 0x00000000, 0x00050051, 0x00000006,
    0x00000010, 0x0000000d, 0x00000001, 0x00070050, 0x00000012, 0x00000020, 0x0000000f, 0x00000010,
    0x0000001d, 0x0000000e, 0x00050041, 0x00000021, 0x00000022, 0x00000018, 0x0000001a, 0x0003003e,
    0x00000022, 0x00000020, 0x0004003d, 0x00000007, 0x00000027, 0x00000026, 0x0003003e, 0x00000024,
    0x00000027, 0x000100fd, 0x00010038,
};

static const uint32_t kCornerPinFragmentSpirv[] = {
    0x07230203, 0x00010300, 0x000d000b, 0x0000001d, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0007000f, 0x00000004, 0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x00000011, 0x00030010,
    0x00000004, 0x00000007, 0x00040047, 0x00000009, 0x0000001e, 0x00000000, 0x00040047, 0x0000000d,
    0x00000021, 0x00000000, 0x00040047, 0x0000000d, 0x00000022, 0x00000002, 0x00040047, 0x00000011,
    0x0000001e, 0x00000000, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016,
    0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000004, 0x00040020, 0x00000008,
    0x00000003, 0x00000007, 0x0004003b, 0x00000008, 0x00000009, 0x00000003, 0x00090019, 0x0000000a,
    0x00000006, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000000, 0x0003001b,
    0x0000000b, 0x0000000a, 0x00040020, 0x0000000c, 0x00000000, 0x0000000b, 0x0004003b, 0x0000000c,
    0x0000000d, 0x00000000, 0x00040017, 0x0000000f, 0x00000006, 0x00000003, 0x00040020, 0x00000010,
    0x00000001, 0x0000000f, 0x0004003b, 0x00000010, 0x00000011, 0x00000001, 0x00040017, 0x00000012,
    0x00000006, 0x00000002, 0x00040015, 0x00000015, 0x00000020, 0x00000000, 0x0004002b, 0x00000015,
    0x00000016, 0x00000002, 0x00040020, 0x00000017, 0x00000001, 0x00000006, 0x00050036, 0x00000002,
    0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003d, 0x0000000b, 0x0000000e,
    0x0000000d, 0x0004003d, 0x0000000f, 0x00000013, 0x00000011, 0x0007004f, 0x00000012, 0x00000014,
    0x00000013, 0x00000013, 0x00000000, 0x00000001, 0x00050041, 0x00000017, 0x00000018, 0x00000011,
    0x00000016, 0x0004003d, 0x00000006, 0x00000019, 0x00000018, 0x00050050, 0x00000012, 0x0000001a,
    0x00000019, 0x00000019, 0x00050088, 0x00000012, 0x0000001b, 0x00000014, 0x0000001a, 0x00050057,
    0x00000007, 0x0000001c, 0x0000000e, 0x0000001b, 0x0003003e, 0x00000009, 0x0000001c, 0x000100fd,
    0x00010038,
};

static constexpr char CORNER_PIN_WGSL_VERTEX_SHADER[] = R"(
struct VertexIn {
  @location(0) aPosition: vec2f,
  @location(1) aTextureCoord: vec3f,
};

struct VertexOut {
  @builtin(position) position: vec4f,
  @location(0) vertexColor: vec3f,
};

@vertex
fn main(in: VertexIn) -> VertexOut {
  var out: VertexOut;
  out.position = vec4f(in.aPosition, 0.0, 1.0);
  out.vertexColor = in.aTextureCoord;
  return out;
}
)";

static constexpr char CORNER_PIN_WGSL_FRAGMENT_SHADER[] = R"(
@group(2) @binding(0) var sTexture: texture_2d<f32>;
@group(2) @binding(1) var sTextureSampler: sampler;

struct VertexOut {
  @builtin(position) position: vec4f,
  @location(0) vertexColor: vec3f,
};

@fragment
fn main(in: VertexOut) -> @location(0) vec4f {
  let uv = in.vertexColor.xy / in.vertexColor.z;
  return textureSample(sTexture, sTextureSampler, uv);
}
)";

static constexpr char CORNER_PIN_HLSL_VERTEX_SHADER[] = R"(
struct VSIn {
  float2 aPosition : TEXCOORD0;
  float3 aTextureCoord : TEXCOORD1;
};

struct VSOut {
  float4 position : SV_Position;
  float3 vertexColor : TEXCOORD0;
};

VSOut main(VSIn input) {
  VSOut output;
  output.position = float4(input.aPosition.x, -input.aPosition.y, 0.0, 1.0);
  output.vertexColor = input.aTextureCoord;
  return output;
}
)";

static constexpr char CORNER_PIN_HLSL_FRAGMENT_SHADER[] = R"(
Texture2D sTexture : register(t0);
SamplerState sTextureSampler : register(s0);

struct PSIn {
  float4 position : SV_Position;
  float3 vertexColor : TEXCOORD0;
};

float4 main(PSIn input) : SV_Target {
  float2 uv = input.vertexColor.xy / input.vertexColor.z;
  return sTexture.Sample(sTextureSampler, uv);
}
)";

static const NativeShaderStage kVertexSource = {CORNER_PIN_VERTEX_SHADER,
                                                CORNER_PIN_MSL_VERTEX_SHADER,
                                                kCornerPinVertexSpirv,
                                                sizeof(kCornerPinVertexSpirv) / sizeof(uint32_t),
                                                CORNER_PIN_WGSL_VERTEX_SHADER,
                                                CORNER_PIN_HLSL_VERTEX_SHADER,
                                                {}};

static const NativeShaderStage kFragmentSource = {
    CORNER_PIN_FRAGMENT_SHADER,
    CORNER_PIN_MSL_FRAGMENT_SHADER,
    kCornerPinFragmentSpirv,
    sizeof(kCornerPinFragmentSpirv) / sizeof(uint32_t),
    CORNER_PIN_WGSL_FRAGMENT_SHADER,
    CORNER_PIN_HLSL_FRAGMENT_SHADER,
    {}};

std::shared_ptr<CornerPinEffect> CornerPinEffect::Make(EffectCache* cache, const Point& upperLeft,
                                                       const Point& upperRight,
                                                       const Point& lowerRight,
                                                       const Point& lowerLeft) {
  return std::shared_ptr<CornerPinEffect>(
      new CornerPinEffect(cache, upperLeft, upperRight, lowerRight, lowerLeft));
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
  std::shared_ptr<ShaderModule> vertexShader = nullptr;
  std::shared_ptr<ShaderModule> fragmentShader = nullptr;
  if (!CreateNativeShaderModules(gpu, kVertexSource, kFragmentSource, &vertexShader,
                                 &fragmentShader)) {
    return nullptr;
  }
  RenderPipelineDescriptor descriptor = {};
  VertexBufferLayout vertexLayout(
      {{"aPosition", VertexFormat::Float2}, {"aTextureCoord", VertexFormat::Float3}});
  descriptor.vertex.bufferLayouts = {vertexLayout};
  descriptor.vertex.module = vertexShader;
  descriptor.fragment.module = fragmentShader;
  descriptor.fragment.colorAttachments.push_back({});
  descriptor.multisample.count = MSAA_SAMPLE_COUNT;
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
                                outputTexture->format(), false, MSAA_SAMPLE_COUNT,
                                TextureUsage::RENDER_ATTACHMENT);
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
