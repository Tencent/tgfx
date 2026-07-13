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
#include <map>
#include <string>
#include "GlassShaderFragments.h"
#include "core/utils/Log.h"
#include "layers/RuntimeEffectUtils.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {

// --- Shared vertex helpers ---

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

static std::string GetShaderVersion(bool isDesktop) {
  return isDesktop ? "#version 150\n\n" : "#version 300 es\n\n";
}

// --- GlassRefractionEffect implementation ---

GlassRefractionEffect::GlassRefractionEffect(float glassWidth, float glassHeight, float halfWidth,
                                             float halfHeight, float cornerRadius, float minHalf,
                                             float innerHalfWidth, float innerHalfHeight,
                                             float innerRadius, float glassThickness,
                                             float refractionFactor, float dispersion, float splay,
                                             float depthRatio, float lightAngle,
                                             float lightIntensity, GlassShapeType shapeType)
    : RuntimeEffect({}), _glassWidth(glassWidth), _glassHeight(glassHeight), _halfWidth(halfWidth),
      _halfHeight(halfHeight), _cornerRadius(cornerRadius), _minHalf(minHalf),
      _innerHalfWidth(innerHalfWidth), _innerHalfHeight(innerHalfHeight), _innerRadius(innerRadius),
      _glassThickness(glassThickness), _refractionFactor(refractionFactor), _dispersion(dispersion),
      _splay(splay), _depthRatio(depthRatio), _lightAngle(lightAngle),
      _lightIntensity(lightIntensity), _shapeType(shapeType) {
}

std::string GlassRefractionEffect::BuildFragmentShader(GlassShapeType shapeType, bool isDesktop) {
  std::string code;
  code += isDesktop ? "#version 150\n\n" : "#version 300 es\n\n";
  code += GLASS_SHADER_HEADER;
  switch (shapeType) {
    case GlassShapeType::RoundedRect:
      code += "#define GLASS_USE_AXIS_MIX\n";
      code += GLASS_SDF_ROUNDED_RECT;
      break;
    case GlassShapeType::Ellipse:
      code += "#define GLASS_USE_AXIS_MIX\n";
      code += GLASS_SDF_ELLIPSE;
      break;
    case GlassShapeType::Star:
    case GlassShapeType::AlphaMask:
      code += GLASS_SDF_ALPHA_MASK;
      break;
  }
  code += GLASS_SHADER_MAIN;
  return code;
}

std::shared_ptr<RenderPipeline> GlassRefractionEffect::CreatePipeline(GPU* gpu,
                                                                      GlassShapeType shapeType) {
  // Static cache keyed by (GPU, shapeType) to avoid shader recompilation on parameter changes.
  static std::map<std::pair<GPU*, GlassShapeType>, std::shared_ptr<RenderPipeline>> pipelineCache;
  auto key = std::make_pair(gpu, shapeType);
  auto it = pipelineCache.find(key);
  if (it != pipelineCache.end() && it->second != nullptr) {
    return it->second;
  }

  auto info = gpu->info();
  auto isDesktop = info->version.find("OpenGL ES") == std::string::npos;
  ShaderModuleDescriptor vertexModule = {};
  std::string vertexCode = GetShaderVersion(isDesktop);
  vertexCode += GLASS_VERTEX_SHADER;
  vertexModule.code = vertexCode;
  vertexModule.stage = ShaderStage::Vertex;
  auto vertexShader = gpu->createShaderModule(vertexModule);
  if (vertexShader == nullptr) {
    return nullptr;
  }
  ShaderModuleDescriptor fragmentModule = {};
  fragmentModule.code = BuildFragmentShader(shapeType, isDesktop);
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
  // Only AlphaMask and Star shapes need MSAA for edge anti-aliasing.
  // RoundedRect and Ellipse use smoothstep-based AA in the shader.
  bool useMSAA = (shapeType == GlassShapeType::AlphaMask || shapeType == GlassShapeType::Star);
  if (useMSAA) {
    descriptor.multisample.count = MSAA_SAMPLE_COUNT;
  }
  BindingEntry sourceBinding = {"uSource", 0};
  descriptor.layout.textureSamplers.push_back(sourceBinding);
  if (shapeType == GlassShapeType::AlphaMask) {
    BindingEntry maskBinding = {"uMask", 1};
    descriptor.layout.textureSamplers.push_back(maskBinding);
  }
  BindingEntry uniformBlockBinding = {"GlassUniforms", 0};
  descriptor.layout.uniformBlocks.push_back(uniformBlockBinding);
  auto pipeline = gpu->createRenderPipeline(descriptor);
  if (pipeline != nullptr) {
    pipelineCache[key] = pipeline;
  }
  return pipeline;
}

bool GlassRefractionEffect::onDraw(CommandEncoder* encoder,
                                   const std::vector<std::shared_ptr<Texture>>& inputTextures,
                                   std::shared_ptr<Texture> outputTexture,
                                   const Point& offset) const {
  if (inputTextures.empty()) {
    LOGE("GlassRefractionEffect: no input textures!");
    return false;
  }
  auto gpu = encoder->gpu();

  auto pipeline = CreatePipeline(gpu, _shapeType);
  if (pipeline == nullptr) {
    LOGE("GlassRefractionEffect: failed to create pipeline!");
    return false;
  }

  // For MSAA shapes, create a multisampled render target and resolve to outputTexture.
  // For non-MSAA shapes, render directly to outputTexture.
  std::shared_ptr<RenderPass> renderPass;
  std::shared_ptr<Texture> msaaTexture = nullptr;
  if (needsMSAA()) {
    TextureDescriptor textureDesc(outputTexture->width(), outputTexture->height(),
                                  outputTexture->format(), false, MSAA_SAMPLE_COUNT,
                                  TextureUsage::RENDER_ATTACHMENT);
    msaaTexture = gpu->createTexture(textureDesc);
    if (msaaTexture == nullptr) {
      LOGE("GlassRefractionEffect: failed to create MSAA texture!");
      return false;
    }
    RenderPassDescriptor renderPassDesc(msaaTexture, LoadAction::Clear, StoreAction::Store,
                                        PMColor::Transparent(), outputTexture);
    renderPass = encoder->beginRenderPass(renderPassDesc);
  } else {
    RenderPassDescriptor renderPassDesc(outputTexture, LoadAction::Clear, StoreAction::Store,
                                        PMColor::Transparent());
    renderPass = encoder->beginRenderPass(renderPassDesc);
  }
  if (renderPass == nullptr) {
    return false;
  }

  renderPass->setPipeline(pipeline);

  // Create and fill vertex buffer
  auto vertexBuffer = gpu->createBuffer(GLASS_VERTEX_SIZE, GPUBufferUsage::VERTEX);
  if (vertexBuffer == nullptr) {
    return false;
  }
  auto vertices = static_cast<float*>(vertexBuffer->map());
  if (vertices == nullptr) {
    return false;
  }
  CollectFullQuadVertices(inputTextures[0].get(), outputTexture.get(), offset, vertices);
  vertexBuffer->unmap();
  renderPass->setVertexBuffer(0, vertexBuffer);

  // Cache sampler to avoid per-frame GPU resource allocation.
  if (!cachedSampler) {
    SamplerDescriptor samplerDesc(AddressMode::ClampToEdge, AddressMode::ClampToEdge,
                                  FilterMode::Linear, FilterMode::Linear, MipmapMode::None);
    cachedSampler = gpu->createSampler(samplerDesc);
  }
  renderPass->setTexture(0, inputTextures[0], cachedSampler);

  // Bind mask texture (binding 1) for AlphaMask shape type
  if (_shapeType == GlassShapeType::AlphaMask) {
    if (inputTextures.size() > 1) {
      renderPass->setTexture(1, inputTextures[1], cachedSampler);
    } else {
      LOGE("GlassRefractionEffect: AlphaMask but no mask texture in inputTextures! size=%zu",
           inputTextures.size());
    }
  }

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

  // Convert light angle from degrees to radians.
  float lightAngleRad = _lightAngle * 3.14159265358979323846f / 180.0f;

  // std140 layout: 5 vec4s = 20 floats (80 bytes)
  size_t uniformSize = 20 * sizeof(float);
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
  // uParams4: splay, depthRatio, lightAngleRad, lightIntensity
  uniformData[16] = _splay;
  uniformData[17] = _depthRatio;
  uniformData[18] = lightAngleRad;
  uniformData[19] = _lightIntensity;
  uniformBuffer->unmap();
  renderPass->setUniformBuffer(0, uniformBuffer, 0, uniformSize);

  renderPass->draw(PrimitiveType::TriangleStrip, 4);
  renderPass->end();
  return true;
}

}  // namespace tgfx
