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
#include <cstdio>
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

// The fragment shader uses two coordinate systems:
// - vTexCoord: UV for sampling the source (background) texture, range [0,1] over the source image.
// - dispMapUV: UV for sampling the displacement map, computed from vTexCoord using uDispMapOffset
//   and uDispMapScale. The displacement map covers the glass layer region, which is a sub-region
//   of the source texture.
// The displacement offset is in pixels, converted to source texture UV space using uPixelToUV.
static constexpr char GLASS_FRAGMENT_SHADER[] = R"(
    precision mediump float;
    in vec2 vTexCoord;
    uniform sampler2D uSource;
    uniform sampler2D uDisplacement;
    layout(std140) uniform GlassUniforms {
        vec2 uDispMapOffset;
        vec2 uDispMapScale;
        vec2 uPixelToUV;
        float uMaxDispPixels;
        float uDispersion;
    };
    out vec4 tgfx_FragColor;
    void main() {
        vec2 dispMapUV = (vTexCoord - uDispMapOffset) * uDispMapScale;
        if (dispMapUV.x < 0.0 || dispMapUV.x > 1.0 || dispMapUV.y < 0.0 || dispMapUV.y > 1.0) {
            tgfx_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
        vec2 disp = texture(uDisplacement, dispMapUV).rg;
        vec2 offsetPixels = (disp - 0.5) * 2.0 * uMaxDispPixels;
        vec2 offset = offsetPixels * uPixelToUV;
        if (uDispersion < 0.001) {
            tgfx_FragColor = texture(uSource, vTexCoord + offset);
        } else {
            float scaleR = 1.0 + uDispersion;
            float scaleB = 1.0 - uDispersion;
            float r = texture(uSource, vTexCoord + offset * scaleR).r;
            float g = texture(uSource, vTexCoord + offset).g;
            float b = texture(uSource, vTexCoord + offset * scaleB).b;
            float a = texture(uSource, vTexCoord + offset).a;
            tgfx_FragColor = vec4(r, g, b, a);
        }
    }
)";

static std::string GetFinalShaderCode(const char* codeSnippet, bool isDesktop) {
  if (isDesktop) {
    return std::string("#version 150\n\n") + codeSnippet;
  }
  return std::string("#version 300 es\n\n") + codeSnippet;
}

GlassRefractionEffect::GlassRefractionEffect(std::shared_ptr<Image> displacementMap,
                                             float displacementScale, float dispersion,
                                             float glassWidth, float glassHeight)
    : RuntimeEffect({displacementMap}), _displacementScale(displacementScale),
      _dispersion(dispersion), _glassWidth(glassWidth), _glassHeight(glassHeight) {
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
  BindingEntry dispBinding = {"uDisplacement", 1};
  descriptor.layout.textureSamplers.push_back(sourceBinding);
  descriptor.layout.textureSamplers.push_back(dispBinding);
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
  if (inputTextures.size() < 2) {
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

  // Bind displacement map texture (binding 1)
  SamplerDescriptor dispSamplerDesc(AddressMode::ClampToEdge, AddressMode::ClampToEdge,
                                    FilterMode::Linear, FilterMode::Linear, MipmapMode::None);
  auto dispSampler = gpu->createSampler(dispSamplerDesc);
  renderPass->setTexture(1, inputTextures[1], dispSampler);

  // Compute UV mapping between source texture and displacement map.
  // The displacement map covers the glass layer region, which may be a sub-region of the source.
  // Source texture UV [0,1] maps to the full background image.
  // Displacement map UV [0,1] maps to the glass layer bounds within that background.
  // Since the source texture is the background cropped/filtered region and the glass layer
  // content is centered within it, we assume the displacement map aligns with the source center.
  auto sourceWidth = static_cast<float>(inputTextures[0]->width());
  auto sourceHeight = static_cast<float>(inputTextures[0]->height());

  // dispMapOffset: source UV where the displacement map's top-left corner starts
  // dispMapScale: ratio to convert source UV range to displacement map UV [0,1]
  float dispMapOffsetX =
      (_glassWidth < sourceWidth) ? (1.0f - _glassWidth / sourceWidth) * 0.5f : 0.0f;
  float dispMapOffsetY =
      (_glassHeight < sourceHeight) ? (1.0f - _glassHeight / sourceHeight) * 0.5f : 0.0f;
  float dispMapScaleX = (_glassWidth > 0.0f) ? sourceWidth / _glassWidth : 1.0f;
  float dispMapScaleY = (_glassHeight > 0.0f) ? sourceHeight / _glassHeight : 1.0f;

  // pixelToUV: converts pixel displacement to source texture UV offset.
  // Displacement is computed in glass-pixel units, so convert using glass dimensions.
  float pixelToUVx = 1.0f / sourceWidth;
  float pixelToUVy = 1.0f / sourceHeight;
  fprintf(stderr,
          "[GlassRefraction] source=%.0fx%.0f glass=%.0fx%.0f pxToUV=(%.6f,%.6f) maxDisp=%.1f\n",
          sourceWidth, sourceHeight, _glassWidth, _glassHeight, pixelToUVx, pixelToUVy,
          _displacementScale);

  // std140 layout: vec2 + vec2 + vec2 + float + float = 8 floats (32 bytes, naturally aligned)
  size_t uniformSize = 8 * sizeof(float);
  auto uniformBuffer = gpu->createBuffer(uniformSize, GPUBufferUsage::UNIFORM);
  if (uniformBuffer == nullptr) {
    return false;
  }
  auto uniformData = static_cast<float*>(uniformBuffer->map());
  if (uniformData == nullptr) {
    return false;
  }
  uniformData[0] = dispMapOffsetX;      // uDispMapOffset.x
  uniformData[1] = dispMapOffsetY;      // uDispMapOffset.y
  uniformData[2] = dispMapScaleX;       // uDispMapScale.x
  uniformData[3] = dispMapScaleY;       // uDispMapScale.y
  uniformData[4] = pixelToUVx;          // uPixelToUV.x
  uniformData[5] = pixelToUVy;          // uPixelToUV.y
  uniformData[6] = _displacementScale;  // uMaxDispPixels (in pixels, not normalized)
  uniformData[7] = _dispersion;         // uDispersion
  uniformBuffer->unmap();
  renderPass->setUniformBuffer(0, uniformBuffer, 0, uniformSize);

  renderPass->draw(PrimitiveType::TriangleStrip, 4);
  renderPass->end();
  return true;
}

}  // namespace tgfx
