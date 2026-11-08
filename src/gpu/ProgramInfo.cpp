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
#include "gpu/aot/bundle/PrecompiledShaderCache.h"
#include "gpu/aot/select/PrecompiledProgramCreator.h"
#include "gpu/resources/RenderTarget.h"
#include "gpu/shaders/contract/ShaderProfileResolver.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {

static void EncodeStencilFace(BytesKey& key, const StencilDescriptor& face) {
  key.write(static_cast<uint32_t>(face.compare));
  key.write(static_cast<uint32_t>(face.failOp));
  key.write(static_cast<uint32_t>(face.depthFailOp));
  key.write(static_cast<uint32_t>(face.passOp));
}

ProgramInfo::ProgramInfo(RenderTarget* renderTarget, GeometryProcessor* geometryProcessor,
                         std::vector<FragmentProcessor*> fragmentProcessors,
                         size_t numColorProcessors, XferProcessor* xferProcessor,
                         BlendMode blendMode)
    : renderTarget(renderTarget), geometryProcessor(geometryProcessor),
      fragmentProcessors(std::move(fragmentProcessors)), numColorProcessors(numColorProcessors),
      xferProcessor(xferProcessor), blendMode(blendMode) {
  hasCoverageForBlend = numColorProcessors < this->fragmentProcessors.size();
  updateProcessorIndices();
}

ProgramInfo::ProgramInfo(RenderTarget* renderTarget, GeometryProcessor* geometryProcessor,
                         XferProcessor* xferProcessor, BlendMode blendMode,
                         bool blendFormulaHasCoverage)
    : renderTarget(renderTarget), geometryProcessor(geometryProcessor),
      xferProcessor(xferProcessor), blendMode(blendMode),
      hasCoverageForBlend(blendFormulaHasCoverage) {
  updateProcessorIndices();
}

void ProgramInfo::updateProcessorIndices() {
  int index = 0;
  processorIndices[geometryProcessor] = index++;
  for (auto& fragmentProcessor : fragmentProcessors) {
    FragmentProcessor::Iter iter(fragmentProcessor);
    const FragmentProcessor* fp = iter.next();
    while (fp) {
      processorIndices[fp] = index++;
      fp = iter.next();
    }
  }
  processorIndices[getXferProcessor()] = index++;
}

const XferProcessor* ProgramInfo::getXferProcessor() const {
  if (xferProcessor == nullptr) {
    return EmptyXferProcessor::GetInstance();
  }
  return xferProcessor;
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
  if (xferProcessor != nullptr || blendMode == BlendMode::Src) {
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

int ProgramInfo::getProcessorIndex(const Processor* processor) const {
  auto result = processorIndices.find(processor);
  if (result == processorIndices.end()) {
    return -1;
  }
  return result->second;
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
  auto context = renderTarget->getContext();
  geometryProcessor->computeProcessorKey(context, &key);
  for (const auto& processor : fragmentProcessors) {
    processor->computeProcessorKey(context, &key);
  }
  // The color/coverage boundary is semantic, not structural: the same processor sequence with a
  // different boundary composites differently (a color FP replaces the paint color; a coverage FP
  // modulates the draw coverage). The cache lookup happens before any matcher or validation can
  // re-check it, so the boundary itself must ride the key or a hot hit can return a program built
  // for the other role.
  key.write(static_cast<uint32_t>(numColorProcessors));
  if (xferProcessor != nullptr) {
    xferProcessor->computeProcessorKey(context, &key);
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

std::shared_ptr<Program> ProgramInfo::getProgram() const {
  auto context = renderTarget->getContext();
  auto globalCache = context->globalCache();

  // The plain key only ever holds generic programs (the creator caches a specialized program
  // under its structure-bearing key), so this fast path is correct for every chain shape.
  const auto& key = programKey();
  auto program = globalCache->findProgram(key);
  if (program != nullptr) {
    return program;
  }
  // Precompiled artifacts are the only program source: a miss drops the draw (see
  // StandardDrawOp::prepare).
  program = PrecompiledProgramCreator::CreateProgram(context, this);
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
  for (size_t i = 0; i < geometryProcessor->numTextureSamplers(); i++) {
    SamplerInfo sampler = {geometryProcessor->textureAt(i), geometryProcessor->samplerStateAt(i)};
    samplers.push_back(sampler);
  }
  FragmentProcessor::Iter iter(this);
  const FragmentProcessor* fp = iter.next();
  while (fp) {
    for (size_t i = 0; i < fp->numTextureSamplers(); ++i) {
      SamplerInfo sampler = {fp->textureAt(i), fp->samplerStateAt(i)};
      samplers.push_back(sampler);
    }
    fp = iter.next();
  }
  auto dstTextureView = xferProcessor != nullptr ? xferProcessor->dstTextureView() : nullptr;
  if (dstTextureView != nullptr) {
    SamplerInfo sampler = {dstTextureView->getTexture(), {}};
    samplers.push_back(sampler);
  }
  return samplers;
}
}  // namespace tgfx
