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

static constexpr char GLASS_FRAGMENT_SHADER[] = R"(
    precision mediump float;
    in vec2 vTexCoord;
    uniform sampler2D uSource;
    uniform sampler2D uDisplacement;
    layout(std140) uniform GlassUniforms {
        float uScale;
        float uDispersion;
    };
    out vec4 tgfx_FragColor;
    void main() {
        vec2 disp = texture(uDisplacement, vTexCoord).rg;
        vec2 offset = (disp - 0.5) * 2.0 * uScale;
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
                                             float displacementScale, float dispersion)
    : RuntimeEffect({displacementMap}), _displacementScale(displacementScale),
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

  // Full-screen quad in NDC with texture coordinates covering the source region
  // Vertex order: bottom-left, bottom-right, top-left, top-right (TriangleStrip)
  float left = offset.x;
  float top = offset.y;
  float right = offset.x + sourceWidth;
  float bottom = offset.y + sourceHeight;

  // Convert pixel coordinates to NDC [-1, 1]
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

  // Create and fill uniform buffer with scale and dispersion
  // Pack both floats into one buffer: [scale, dispersion]
  size_t uniformSize = 2 * sizeof(float);
  auto uniformBuffer = gpu->createBuffer(uniformSize, GPUBufferUsage::UNIFORM);
  if (uniformBuffer == nullptr) {
    return false;
  }
  auto uniformData = static_cast<float*>(uniformBuffer->map());
  if (uniformData == nullptr) {
    return false;
  }
  // Normalize displacement scale relative to texture dimensions
  auto sourceWidth = static_cast<float>(inputTextures[0]->width());
  uniformData[0] = _displacementScale / sourceWidth;
  uniformData[1] = _dispersion;
  uniformBuffer->unmap();
  renderPass->setUniformBuffer(0, uniformBuffer, 0, uniformSize);

  renderPass->draw(PrimitiveType::TriangleStrip, 4);
  renderPass->end();
  return true;
}

}  // namespace tgfx
