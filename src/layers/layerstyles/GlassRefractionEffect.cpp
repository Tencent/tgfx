/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "GlassRefractionEffect.h"
#include <string>
#include "tgfx/gpu/GPU.h"

namespace tgfx {

// 4 vertices * (2 position + 2 texcoord) = 16 floats
static constexpr size_t GLASS_VERTEX_SIZE = 16 * sizeof(float);
static constexpr int MSAA_SAMPLE_COUNT = 4;

static constexpr char GLASS_VERTEX_SHADER[] = R"(
    in vec2 aPosition;
    in vec2 aTextureCoord;
    out vec2 vTexCoord;
    void main() {
        gl_Position = vec4(aPosition, 0.0, 1.0);
        vTexCoord = aTextureCoord;
    }
)";

// The fragment shader computes the glass refraction displacement entirely on the GPU, without
// any intermediate displacement map texture. For each pixel it:
//   1. Converts the source texture UV to glass pixel coordinates (centered at origin).
//   2. Evaluates the rounded-rectangle SDF for the outer and inner shapes.
//   3. If the pixel is in the edge band (between outer and inner shapes), computes the displacement:
//      direction = radial toward center, distance = glassThickness * refractionFactor * edgeFactor².
//   4. Converts the displacement (in glass pixels) to a source texture UV offset.
//   5. Applies chromatic dispersion by sampling R/G/B channels at different UV offsets.
//
// Uniform layout (std140, 4 vec4s = 64 bytes):
//   uParams0: glassOffsetX, glassOffsetY, glassScaleX, glassScaleY
//   uParams1: halfW, halfH, cornerRadius, minHalf
//   uParams2: innerHalfW, innerHalfH, innerRadius, glassThickness
//   uParams3: refractionFactor, dispersion, 1/sourceWidth, 1/sourceHeight
static constexpr char GLASS_FRAGMENT_SHADER[] = R"(
    precision highp float;
    in vec2 vTexCoord;
    uniform sampler2D uSource;
    layout(std140) uniform GlassUniforms {
        vec4 uParams0;  // glassOffsetX, glassOffsetY, glassScaleX, glassScaleY
        vec4 uParams1;  // halfW, halfH, cornerRadius, minHalf
        vec4 uParams2;  // innerHalfW, innerHalfH, innerRadius, glassThickness
        vec4 uParams3;  // refractionFactor, dispersion, 1/sourceWidth, 1/sourceHeight
    };
    out vec4 tgfx_FragColor;

    float roundedRectSDF(float px, float py, float hw, float hh, float r) {
        float qx = abs(px) - hw + r;
        float qy = abs(py) - hh + r;
        float outsideDist = sqrt(max(qx, 0.0) * max(qx, 0.0) + max(qy, 0.0) * max(qy, 0.0));
        float insideDist = min(max(qx, qy), 0.0);
        return outsideDist + insideDist - r;
    }

    void main() {
        float halfW = uParams1.x;
        float halfH = uParams1.y;
        float cornerRadius = uParams1.z;
        float innerHalfW = uParams2.x;
        float innerHalfH = uParams2.y;
        float innerRadius = uParams2.z;
        float glassThickness = uParams2.w;
        float refractionFactor = uParams3.x;
        float dispersion = uParams3.y;
        float invSourceW = uParams3.z;
        float invSourceH = uParams3.w;

        // Convert source UV to glass pixel coordinates centered at origin.
        vec2 glassUV = (vTexCoord - uParams0.xy) * uParams0.zw;
        glassUV = vec2(glassUV.x, 1.0 - glassUV.y);
        vec2 glassPixel = glassUV * vec2(halfW * 2.0, halfH * 2.0);
        float px = glassPixel.x - halfW;
        float py = glassPixel.y - halfH;

        vec2 uvOffset = vec2(0.0);

        float outerSDF = roundedRectSDF(px, py, halfW, halfH, cornerRadius);
        float innerSDF = roundedRectSDF(px, py, innerHalfW, innerHalfH, innerRadius);

        // Only apply refraction in the edge band between outer and inner shapes.
        if (outerSDF < 0.0 && innerSDF >= 0.0) {
            float edgeDist = -outerSDF;
            float totalDist = edgeDist + innerSDF;
            float xNorm = (totalDist > 0.001) ? edgeDist / totalDist : 0.0;
            xNorm = min(xNorm, 1.0);

            // Edge factor: 1.0 at outer edge (xNorm=0), 0.0 at flat region (xNorm=1).
            float edgeFactor = 1.0 - xNorm;
            // Offset distance: glassThickness * refractionFactor * edgeFactor² * 2.
            float offsetDist = glassThickness * refractionFactor * edgeFactor * edgeFactor * 2.0;

            // Refraction direction: radial toward center.
            float dirLen = sqrt(px * px + py * py);
            if (dirLen > 0.001) {
                vec2 refractDir = vec2(-px / dirLen, -py / dirLen);
                float dispX = refractDir.x * offsetDist;
                float dispY = refractDir.y * offsetDist;
                uvOffset = vec2(dispX * invSourceW, -dispY * invSourceH);
            }
        }

        vec2 uvR, uvG, uvB;
        if (dispersion < 0.01) {
            uvR = uvG = uvB = clamp(vTexCoord + uvOffset, vec2(0.0), vec2(1.0));
        } else {
            uvR = clamp(vTexCoord + uvOffset * (1.0 + dispersion), vec2(0.0), vec2(1.0));
            uvG = clamp(vTexCoord + uvOffset, vec2(0.0), vec2(1.0));
            uvB = clamp(vTexCoord + uvOffset * (1.0 - dispersion), vec2(0.0), vec2(1.0));
        }
        float r = texture(uSource, uvR).r;
        float g = texture(uSource, uvG).g;
        float b = texture(uSource, uvB).b;
        float a = texture(uSource, uvG).a;
        tgfx_FragColor = vec4(r, g, b, a);
    }
)";

static std::string GetFinalShaderCode(const char* codeSnippet, bool isDesktop) {
  if (isDesktop) {
    return std::string("#version 150\n\n") + codeSnippet;
  }
  return std::string("#version 300 es\n\n") + codeSnippet;
}

GlassRefractionEffect::GlassRefractionEffect(float glassWidth, float glassHeight, float halfWidth,
                                             float halfHeight, float cornerRadius, float minHalf,
                                             float innerHalfWidth, float innerHalfHeight,
                                             float innerRadius, float glassThickness,
                                             float refractionFactor, float dispersion)
    : RuntimeEffect({}), _glassWidth(glassWidth), _glassHeight(glassHeight), _halfWidth(halfWidth),
      _halfHeight(halfHeight), _cornerRadius(cornerRadius), _minHalf(minHalf),
      _innerHalfWidth(innerHalfWidth), _innerHalfHeight(innerHalfHeight), _innerRadius(innerRadius),
      _glassThickness(glassThickness), _refractionFactor(refractionFactor),
      _dispersion(dispersion) {
}

std::shared_ptr<RenderPipeline> GlassRefractionEffect::createPipeline(GPU* gpu) const {
  auto info = gpu->info();
  auto isDesktop = info->version.find("OpenGL ES") == std::string::npos;
  ShaderModuleDescriptor vertexModule = {};
  vertexModule.code = GetFinalShaderCode(GLASS_VERTEX_SHADER, isDesktop);
  vertexModule.stage = ShaderStage::Vertex;
  auto vertexShader = gpu->createShaderModule(vertexModule);
  if (vertexShader == nullptr) {
    return nullptr;
  }
  ShaderModuleDescriptor fragmentModule = {};
  fragmentModule.code = GetFinalShaderCode(GLASS_FRAGMENT_SHADER, isDesktop);
  fragmentModule.stage = ShaderStage::Fragment;
  auto fragmentShader = gpu->createShaderModule(fragmentModule);
  if (fragmentShader == nullptr) {
    return nullptr;
  }
  RenderPipelineDescriptor descriptor = {};
  VertexBufferLayout vertexLayout(
      {{"aPosition", VertexFormat::Float2}, {"aTextureCoord", VertexFormat::Float2}});
  descriptor.vertex.bufferLayouts = {vertexLayout};
  descriptor.vertex.module = vertexShader;
  descriptor.fragment.module = fragmentShader;
  descriptor.fragment.colorAttachments.push_back({});
  descriptor.multisample.count = MSAA_SAMPLE_COUNT;
  BindingEntry sourceBinding = {"uSource", 0};
  descriptor.layout.textureSamplers.push_back(sourceBinding);
  BindingEntry uniformBlockBinding = {"GlassUniforms", 0};
  descriptor.layout.uniformBlocks.push_back(uniformBlockBinding);
  return gpu->createRenderPipeline(descriptor);
}

void GlassRefractionEffect::collectVertices(const Texture* source, const Texture* target,
                                            const Point& offset, float* vertices) const {
  auto sourceWidth = static_cast<float>(source->width());
  auto sourceHeight = static_cast<float>(source->height());
  auto targetWidth = static_cast<float>(target->width());
  auto targetHeight = static_cast<float>(target->height());

  float left = offset.x;
  float top = offset.y;
  float right = offset.x + sourceWidth;
  float bottom = offset.y + sourceHeight;

  float ndcLeft = 2.0f * left / targetWidth - 1.0f;
  float ndcRight = 2.0f * right / targetWidth - 1.0f;
  float ndcTop = 2.0f * top / targetHeight - 1.0f;
  float ndcBottom = 2.0f * bottom / targetHeight - 1.0f;

  size_t i = 0;
  // Bottom-left
  vertices[i++] = ndcLeft;
  vertices[i++] = ndcBottom;
  vertices[i++] = 0.0f;
  vertices[i++] = 1.0f;
  // Bottom-right
  vertices[i++] = ndcRight;
  vertices[i++] = ndcBottom;
  vertices[i++] = 1.0f;
  vertices[i++] = 1.0f;
  // Top-left
  vertices[i++] = ndcLeft;
  vertices[i++] = ndcTop;
  vertices[i++] = 0.0f;
  vertices[i++] = 0.0f;
  // Top-right
  vertices[i++] = ndcRight;
  vertices[i++] = ndcTop;
  vertices[i++] = 1.0f;
  vertices[i++] = 0.0f;
}

bool GlassRefractionEffect::onDraw(CommandEncoder* encoder,
                                   const std::vector<std::shared_ptr<Texture>>& inputTextures,
                                   std::shared_ptr<Texture> outputTexture,
                                   const Point& offset) const {
  if (inputTextures.empty()) {
    return false;
  }
  auto gpu = encoder->gpu();

  if (cachedPipeline == nullptr) {
    cachedPipeline = createPipeline(gpu);
    if (cachedPipeline == nullptr) {
      return false;
    }
  }

  // Create MSAA render target
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

  renderPass->setPipeline(cachedPipeline);

  // Create and fill vertex buffer
  auto vertexBuffer = gpu->createBuffer(GLASS_VERTEX_SIZE, GPUBufferUsage::VERTEX);
  if (vertexBuffer == nullptr) {
    return false;
  }
  auto vertices = static_cast<float*>(vertexBuffer->map());
  if (vertices == nullptr) {
    return false;
  }
  collectVertices(inputTextures[0].get(), outputTexture.get(), offset, vertices);
  vertexBuffer->unmap();
  renderPass->setVertexBuffer(0, vertexBuffer);

  // Bind source texture (binding 0)
  SamplerDescriptor samplerDesc(AddressMode::ClampToEdge, AddressMode::ClampToEdge,
                                FilterMode::Linear, FilterMode::Linear, MipmapMode::None);
  auto sampler = gpu->createSampler(samplerDesc);
  renderPass->setTexture(0, inputTextures[0], sampler);

  // Compute UV mapping between source texture and glass region.
  auto sourceWidth = static_cast<float>(inputTextures[0]->width());
  auto sourceHeight = static_cast<float>(inputTextures[0]->height());

  float glassOffsetX =
      (_glassWidth < sourceWidth) ? (1.0f - _glassWidth / sourceWidth) * 0.5f : 0.0f;
  float glassOffsetY =
      (_glassHeight < sourceHeight) ? (1.0f - _glassHeight / sourceHeight) * 0.5f : 0.0f;
  float glassScaleX = (_glassWidth > 0.0f) ? sourceWidth / _glassWidth : 1.0f;
  float glassScaleY = (_glassHeight > 0.0f) ? sourceHeight / _glassHeight : 1.0f;

  float invSourceW = 1.0f / sourceWidth;
  float invSourceH = 1.0f / sourceHeight;

  // std140 layout: 4 vec4s = 16 floats (64 bytes)
  size_t uniformSize = 16 * sizeof(float);
  auto uniformBuffer = gpu->createBuffer(uniformSize, GPUBufferUsage::UNIFORM);
  if (uniformBuffer == nullptr) {
    return false;
  }
  auto uniformData = static_cast<float*>(uniformBuffer->map());
  if (uniformData == nullptr) {
    return false;
  }
  // uParams0: glassOffsetX, glassOffsetY, glassScaleX, glassScaleY
  uniformData[0] = glassOffsetX;
  uniformData[1] = glassOffsetY;
  uniformData[2] = glassScaleX;
  uniformData[3] = glassScaleY;
  // uParams1: halfW, halfH, cornerRadius, minHalf
  uniformData[4] = _halfWidth;
  uniformData[5] = _halfHeight;
  uniformData[6] = _cornerRadius;
  uniformData[7] = _minHalf;
  // uParams2: innerHalfW, innerHalfH, innerRadius, glassThickness
  uniformData[8] = _innerHalfWidth;
  uniformData[9] = _innerHalfHeight;
  uniformData[10] = _innerRadius;
  uniformData[11] = _glassThickness;
  // uParams3: refractionFactor, dispersion, 1/sourceWidth, 1/sourceHeight
  uniformData[12] = _refractionFactor;
  uniformData[13] = _dispersion;
  uniformData[14] = invSourceW;
  uniformData[15] = invSourceH;
  uniformBuffer->unmap();
  renderPass->setUniformBuffer(0, uniformBuffer, 0, uniformSize);

  renderPass->draw(PrimitiveType::TriangleStrip, 4);
  renderPass->end();
  return true;
}

}  // namespace tgfx
