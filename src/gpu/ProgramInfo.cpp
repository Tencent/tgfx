/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "ProgramInfo.h"
#include <iomanip>
#include <sstream>
#include "AlignTo.h"
#include "gpu/BlendFormula.h"
#include "gpu/ContextInternal.h"
#include "gpu/GlobalCache.h"
#include "gpu/ProgramWriter.h"
#include "gpu/ShaderCaps.h"
#include "gpu/aot/bundle/PrecompiledShaderCache.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/select/PrecompiledProgramCreator.h"
#include "gpu/aot/select/ProgramKey.h"
#include "gpu/aot/select/ShaderProfileResolver.h"
#include "gpu/resources/RenderTarget.h"
#include "gpu/resources/YUVTextureView.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {

static void EncodeStencilFace(BytesKey& key, const StencilDescriptor& face) {
  key.write(static_cast<uint32_t>(face.compare));
  key.write(static_cast<uint32_t>(face.failOp));
  key.write(static_cast<uint32_t>(face.depthFailOp));
  key.write(static_cast<uint32_t>(face.passOp));
}

ProgramInfo::ProgramInfo(RenderTarget* renderTarget, const GeometryLayout& geometryLayout,
                         const std::optional<DstTextureInfo>& dstTexture, BlendMode blendMode,
                         bool blendFormulaHasCoverage)
    : renderTarget(renderTarget), geometryLayout(geometryLayout), blendMode(blendMode),
      hasCoverageForBlend(blendFormulaHasCoverage) {
  updateXferFacts(dstTexture);
}

void ProgramInfo::updateXferFacts(const std::optional<DstTextureInfo>& dstTexture) {
  // An absent value leaves the output to the fixed-function blend; a present one runs the formula
  // in the shader, its empty proxy selecting the backend's framebuffer fetch.
  xferFacts.dstTexture = dstTexture;
}

Swizzle ProgramInfo::getOutputSwizzle() const {
  return Swizzle::ForWrite(renderTarget->format());
}

int ProgramInfo::getSampleCount() const {
  return renderTarget->sampleCount();
}

PipelineColorAttachment ProgramInfo::getPipelineColorAttachment() const {
  PipelineColorAttachment colorAttachment = {};
  colorAttachment.format = renderTarget->format();
  colorAttachment.colorWriteMask = colorWriteMask;
  if (xferFacts.shaderBlend() || blendMode == BlendMode::Src) {
    return colorAttachment;
  }
  BlendFormula blendFormula = {};
  if (!BlendModeAsCoeff(blendMode, hasCoverageForBlend, &blendFormula)) {
    return colorAttachment;
  }
  colorAttachment.blendEnable = true;
  colorAttachment.srcColorBlendFactor = blendFormula.srcFactor();
  colorAttachment.dstColorBlendFactor = blendFormula.dstFactor();
  colorAttachment.colorBlendOp = blendFormula.operation();
  colorAttachment.srcAlphaBlendFactor = blendFormula.srcFactor();
  colorAttachment.dstAlphaBlendFactor = blendFormula.dstFactor();
  colorAttachment.alphaBlendOp = blendFormula.operation();
  return colorAttachment;
}

std::string ProgramInfo::FormatDiagnosticKey(const BytesKey& key) {
  std::stringstream stream;
  stream << key.size() << ":" << std::hex << std::setfill('0');
  for (size_t index = 0; index < key.size(); ++index) {
    stream << std::setw(8) << key.data()[index];
  }
  return stream.str();
}

const BytesKey& ProgramInfo::programKey() const {
  if (!cachedProgramKey.has_value()) {
    cachedProgramKey = buildProgramKey();
  }
  return *cachedProgramKey;
}

BytesKey ProgramInfo::buildProgramKey() const {
  BytesKey key = {};
  // The geometry's key contribution (see the precompiled-side key): kind, perspective and the
  // live attribute formats.
  key.write(static_cast<uint32_t>(geometryLayout.parameters.kind));
  key.write(static_cast<uint32_t>(geometryLayout.parameters.hasUVPerspective ? 1u : 0u));
  for (const auto& attribute : geometryLayout.vertexAttributes) {
    if (!attribute.empty()) {
      key.write(static_cast<uint32_t>(attribute.format()));
    }
  }
  for (const auto& attribute : geometryLayout.instanceAttributes) {
    if (!attribute.empty()) {
      key.write(static_cast<uint32_t>(attribute.format()));
    }
  }
  // The expression's identity: the lowered IR carries every matching-relevant fact now that the
  // processor list is gone — node kinds, parameters, texture identities, and the color/coverage
  // boundary, which stays semantic (the same sequence on the other side of the boundary
  // composites differently, so it must ride the key or a negative hit can poison the wrong role).
  if (carriedIR != nullptr) {
    HashDrawIR(*carriedIR, &key);
  } else {
    key.write(static_cast<uint32_t>(0));
  }
  // The output formula's contribution, the same encoding the precompiled-side key writes: the
  // shader-stage flag and the sampled destination's identity (a null proxy is the framebuffer
  // fetch; blendMode rides the pipeline state key below).
  key.write(static_cast<uint32_t>(xferFacts.shaderBlend() ? 1u : 0u));
  if (xferFacts.shaderBlend()) {
    const auto& dstProxy = xferFacts.dstTexture->textureProxy;
    auto dstView = dstProxy != nullptr ? dstProxy->getTextureView() : nullptr;
    if (dstView != nullptr) {
      TextureView::ComputeTextureKey(dstView->getTexture(), &key);
    }
  }
  writePipelineStateKey(&key);
  return key;
}

void ProgramInfo::writePipelineStateKey(BytesKey* key) const {
  key->write(static_cast<uint32_t>(blendMode));
  key->write(static_cast<uint32_t>(getOutputSwizzle().asKey()));
  key->write(static_cast<uint32_t>(cullMode));
  key->write(static_cast<uint32_t>(renderTarget->format()));
  // Note: if mask or alphaToCoverage from MultisampleDescriptor are used in the pipeline
  // creation, they must also be encoded here.
  key->write(static_cast<uint32_t>(renderTarget->sampleCount()));
  // Pipelines that share shaders but differ in colour write mask or stencil configuration must
  // resolve to distinct cache entries — otherwise two draws with identical processor trees would
  // silently collapse onto one program and one of them would run with the wrong pipeline state.
  // Keep this section in sync with the fields actually written into RenderPipelineDescriptor.
  key->write(colorWriteMask);
  key->write(static_cast<uint32_t>(depthStencil.format));
  key->write(static_cast<uint32_t>(depthStencil.depthCompare));
  key->write(static_cast<uint32_t>(depthStencil.depthWriteEnabled ? 1 : 0));
  key->write(depthStencil.stencilReadMask);
  key->write(depthStencil.stencilWriteMask);
  EncodeStencilFace(*key, depthStencil.stencilFront);
  EncodeStencilFace(*key, depthStencil.stencilBack);
}

std::shared_ptr<Program> ProgramInfo::getProgram() {
  auto context = renderTarget->getContext();
  // Precompiled artifacts are the only program source: a miss drops the draw (see
  // StandardDrawOp::prepare). The cache lookup lives in the creator because a program's key
  // carries its matched variant and specialization values, which only the match can name. The
  // creator records the selection's data sources on this info, so the upload reads them instead
  // of re-matching.
  auto program = PrecompiledProgramCreator::CreateProgram(context, this);
  if (program == nullptr) {
    LOGE("ProgramInfo::getProgram() Precompiled program unavailable!");
    return nullptr;
  }
  return program;
}

std::shared_ptr<GPUBuffer> ProgramInfo::getUniformBuffer(const Program* program,
                                                         size_t* vertexOffset,
                                                         size_t* fragmentOffset) const {
  DEBUG_ASSERT(renderTarget != nullptr);

  auto globalCache = renderTarget->getContext()->globalCache();
  auto uboOffsetAlignment =
      static_cast<size_t>(renderTarget->getContext()->shaderCaps()->uboOffsetAlignment);
  size_t vertexUniformBufferSize = 0;
  auto vertexUniformData = program->getUniformData(ShaderStage::Vertex);
  if (vertexUniformData != nullptr) {
    vertexUniformBufferSize += vertexUniformData->size();
    vertexUniformBufferSize = AlignTo(vertexUniformBufferSize, uboOffsetAlignment);
  }

  size_t fragmentUniformBufferSize = 0;
  auto fragmentUniformData = program->getUniformData(ShaderStage::Fragment);
  if (fragmentUniformData != nullptr) {
    fragmentUniformBufferSize += fragmentUniformData->size();
  }

  size_t totalUniformBufferSize = vertexUniformBufferSize + fragmentUniformBufferSize;
  std::shared_ptr<GPUBuffer> uniformBuffer = nullptr;
  if (totalUniformBufferSize > 0) {
    size_t lastUniformBufferOffset = 0;
    uniformBuffer =
        globalCache->findOrCreateUniformBuffer(totalUniformBufferSize, &lastUniformBufferOffset);
    if (uniformBuffer != nullptr) {
      auto buffer = static_cast<uint8_t*>(
          uniformBuffer->map(lastUniformBufferOffset, totalUniformBufferSize));
      if (buffer == nullptr) {
        // A failed map leaves no writable backing store: hand back no buffer so the caller
        // aborts the draw instead of the uploads writing through a null pointer.
        return nullptr;
      }
      if (vertexUniformData != nullptr) {
        vertexUniformData->setBuffer(buffer);
        *vertexOffset = lastUniformBufferOffset;
      }
      if (fragmentUniformData != nullptr) {
        fragmentUniformData->setBuffer(buffer + vertexUniformBufferSize);
        *fragmentOffset = lastUniformBufferOffset + vertexUniformBufferSize;
      }
    }
  }

  return uniformBuffer;
}

void ProgramInfo::bindUniformBufferAndUnloadToGPU(const Program* program,
                                                  std::shared_ptr<GPUBuffer> uniformBuffer,
                                                  RenderPass* renderPass, size_t vertexOffset,
                                                  size_t fragmentOffset) const {
  if (uniformBuffer == nullptr) {
    return;
  }

  auto vertexUniformData = program->getUniformData(ShaderStage::Vertex);
  auto fragmentUniformData = program->getUniformData(ShaderStage::Fragment);

  uniformBuffer->unmap();

  if (vertexUniformData != nullptr) {
    renderPass->setUniformBuffer(VERTEX_UBO_BINDING_POINT, uniformBuffer, vertexOffset,
                                 vertexUniformData->size());
    vertexUniformData->setBuffer(nullptr);
  }

  if (fragmentUniformData != nullptr) {
    renderPass->setUniformBuffer(FRAGMENT_UBO_BINDING_POINT, std::move(uniformBuffer),
                                 fragmentOffset, fragmentUniformData->size());
    fragmentUniformData->setBuffer(nullptr);
  }
}

bool ProgramInfo::setUniformsAndSamplers(RenderPass* renderPass, Program* program) const {
  DEBUG_ASSERT(renderTarget != nullptr);
  return program->writer()->write(*this, renderPass, program);
}

Backend ProgramInfo::backend() const {
  return renderTarget->getContext()->backend();
}

std::string ProgramInfo::glProfileTag() const {
  auto shaderCaps = renderTarget->getContext()->shaderCaps();
  return ProfileTag(ResolveGLShaderProfile(shaderCaps->usesPrecisionModifiers));
}

bool ProgramInfo::glProfileMatchesCache() const {
  auto cache = ContextCacheAccess::Cache(renderTarget->getContext());
  return cache->profileTag() == glProfileTag();
}

bool ProgramInfo::samplersAre2D() const {
  for (const auto& sampler : getSamplers()) {
    if (sampler.texture == nullptr) {
      return false;
    }
    // Only TwoD is supported by the precompiled samplers (see the gate in MatchPermutationImpl
    // for where rectangle and External (OES) textures come from).
    if (sampler.texture->type() != TextureType::TwoD) {
      return false;
    }
  }
  return true;
}

std::vector<SamplerInfo> ProgramInfo::getSamplers() const {
  std::vector<SamplerInfo> samplers = {};
  // The geometry's own texture (the glyph atlas); its sampler state is the default one.
  if (geometryLayout.parameters.atlasProxy != nullptr) {
    auto atlasView = geometryLayout.parameters.atlasProxy->getTextureView();
    if (atlasView != nullptr) {
      SamplerInfo sampler = {atlasView->getTexture(), geometryLayout.parameters.atlasSampler};
      samplers.push_back(sampler);
    }
  }
  // The expression's samplers, recorded at lowering in the tree walk's order. A view that has not
  // been instantiated yet contributes nothing unless the processor declared a view-independent
  // count; a YUV view expands to its planes here, exactly as the processor enumeration did.
  if (carriedIR != nullptr) {
    for (const auto& ref : carriedIR->samplers) {
      auto view = ref.textureProxy != nullptr ? ref.textureProxy->getTextureView() : nullptr;
      if (view == nullptr) {
        if (ref.countsWithoutView) {
          samplers.push_back({nullptr, ref.state});
        }
        continue;
      }
      if (view->isYUV()) {
        auto* yuvView = static_cast<const YUVTextureView*>(view.get());
        for (size_t index = 0; index < yuvView->textureCount(); ++index) {
          samplers.push_back({yuvView->getTextureAt(index), ref.state});
        }
      } else {
        samplers.push_back({view->getTexture(), ref.state});
      }
    }
  }
  const auto& dstProxy = xferFacts.dstTexture.has_value() ? xferFacts.dstTexture->textureProxy
                                                          : DstTextureInfo{}.textureProxy;
  auto dstView =
      xferFacts.shaderBlend() && dstProxy != nullptr ? dstProxy->getTextureView() : nullptr;
  if (dstView != nullptr) {
    SamplerInfo sampler = {dstView->getTexture(), {}};
    samplers.push_back(sampler);
  }
  return samplers;
}

size_t ProgramInfo::suppliedColorSamplerCount() const {
  if (carriedIR == nullptr) {
    return 0;
  }
  size_t count = 0;
  for (const auto& ref : carriedIR->samplers) {
    if (ref.coverageSide) {
      continue;
    }
    auto view = ref.textureProxy != nullptr ? ref.textureProxy->getTextureView() : nullptr;
    if (view == nullptr) {
      count += ref.countsWithoutView ? 1 : 0;
      continue;
    }
    count += view->isYUV() ? static_cast<const YUVTextureView*>(view.get())->textureCount() : 1;
  }
  return count;
}
}  // namespace tgfx
