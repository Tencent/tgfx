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
#include "gpu/ProgramBuilder.h"
#include "gpu/ProgramWriter.h"
#include "gpu/ShaderCaps.h"
#include "gpu/aot/bundle/PrecompiledShaderCache.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/select/ProgramKey.h"
#include "gpu/aot/select/ShaderProfileResolver.h"
#include "gpu/processors/EmptyXferProcessor.h"
#include "gpu/processors/GeometryProcessor.h"
#include "gpu/processors/XferProcessor.h"
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

ProgramInfo::ProgramInfo(RenderTarget* renderTarget, GeometryProcessor* geometryProcessor,
                         std::vector<FragmentProcessor*> fragmentProcessors,
                         size_t numColorProcessors, XferProcessor* xferProcessor,
                         BlendMode blendMode)
    : renderTarget(renderTarget), geometryProcessor(geometryProcessor),
      fragmentProcessors(std::move(fragmentProcessors)), numColorProcessors(numColorProcessors),
      xferProcessor(xferProcessor), blendMode(blendMode),
      hasCoverageForBlend(numColorProcessors < this->fragmentProcessors.size()) {
  updateProcessorIndices();
}

const std::vector<Attribute>& ProgramInfo::getVertexAttributes() const {
  if (geometryProcessor != nullptr) {
    return geometryProcessor->vertexAttributes();
  }
  return getGeometryLayout().vertexAttributes;
}

const std::vector<Attribute>& ProgramInfo::getInstanceAttributes() const {
  if (geometryProcessor != nullptr) {
    return geometryProcessor->instanceAttributes();
  }
  return getGeometryLayout().instanceAttributes;
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

int ProgramInfo::getProcessorIndex(const Processor* processor) const {
  auto result = processorIndices.find(processor);
  if (result == processorIndices.end()) {
    return -1;
  }
  return result->second;
}

std::string ProgramInfo::getMangledSuffix(const Processor* processor) const {
  auto processorIndex = getProcessorIndex(processor);
  if (processorIndex == -1) {
    return "";
  }
  return "_P" + std::to_string(processorIndex);
}

void ProgramInfo::updateUniformDataSuffix(UniformData* vertexUniformData,
                                          UniformData* fragmentUniformData,
                                          const Processor* processor) const {
  auto suffix = getMangledSuffix(processor);
  if (vertexUniformData != nullptr) {
    vertexUniformData->nameSuffix = suffix;
  }

  if (fragmentUniformData != nullptr) {
    fragmentUniformData->nameSuffix = suffix;
  }
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
  // A bound xfer processor runs the blend formula in the shader (the JIT route keeps the dst
  // fact on the processor rather than in xferFacts), so the fixed-function blend must stay off
  // there too — porter-duff modes such as SrcIn have no fixed-function equivalent.
  if (xferProcessor != nullptr || xferFacts.shaderBlend() || blendMode == BlendMode::Src) {
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

BytesKey ProgramInfo::programKey() const {
  return buildProgramKey();
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
  if (geometryProcessor != nullptr) {
    // The JIT route: the tree's processor keys plus the shared pipeline-state tail key the
    // program cache; a miss emits the program on the fly through ProgramBuilder.
    BytesKey programKey = {};
    geometryProcessor->computeProcessorKey(context, &programKey);
    for (const auto& processor : fragmentProcessors) {
      processor->computeProcessorKey(context, &programKey);
    }
    if (xferProcessor != nullptr) {
      xferProcessor->computeProcessorKey(context, &programKey);
    }
    writePipelineStateKey(&programKey);
    auto program = context->globalCache()->findProgram(programKey);
    if (program == nullptr) {
      program = ProgramBuilder::CreateProgram(context, this);
      if (program == nullptr) {
        LOGE("ProgramInfo::getProgram() Failed to create the program!");
        return nullptr;
      }
      context->globalCache()->addProgram(programKey, program);
    }
    return program;
  }
  // Without a geometry processor the tree has nothing to build: the draw has no program source.
  LOGE("ProgramInfo::getProgram() the info carries no geometry processor");
  return nullptr;
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
  if (geometryProcessor != nullptr) {
    // The JIT route: the program carries no route writer, so the processor tree itself walks the
    // uniform data (each processor's names suffixed by its index) and the samplers follow the
    // tree's pre-order.
    size_t vertexOffset = 0;
    size_t fragmentOffset = 0;
    auto uniformBuffer = getUniformBuffer(program, &vertexOffset, &fragmentOffset);

    auto vertexUniformData = program->getUniformData(ShaderStage::Vertex);
    auto fragmentUniformData = program->getUniformData(ShaderStage::Fragment);

    auto array = GetRTAdjustArray(renderTarget);
    if (vertexUniformData != nullptr) {
      vertexUniformData->setData(RTAdjustName, array);
    }
    updateUniformDataSuffix(vertexUniformData, fragmentUniformData, geometryProcessor);

    FragmentProcessor::CoordTransformIter coordTransformIter(this);
    geometryProcessor->setData(vertexUniformData, fragmentUniformData, &coordTransformIter);

    for (auto& fragmentProcessor : fragmentProcessors) {
      FragmentProcessor::Iter iter(fragmentProcessor);
      const FragmentProcessor* fp = iter.next();
      while (fp) {
        updateUniformDataSuffix(vertexUniformData, fragmentUniformData, fp);
        fp->setData(vertexUniformData, fragmentUniformData);
        fp = iter.next();
      }
    }
    const auto processor = getXferProcessor();
    updateUniformDataSuffix(vertexUniformData, fragmentUniformData, processor);
    processor->setData(vertexUniformData, fragmentUniformData);
    updateUniformDataSuffix(vertexUniformData, fragmentUniformData, nullptr);

    bindUniformBufferAndUnloadToGPU(program, std::move(uniformBuffer), renderPass, vertexOffset,
                                    fragmentOffset);

    auto samplers = getSamplers();
    unsigned textureBinding = 0;
    auto gpu = renderTarget->getContext()->gpu();
    for (auto& [texture, state] : samplers) {
      SamplerDescriptor descriptor(ToAddressMode(state.tileModeX), ToAddressMode(state.tileModeY),
                                   state.minFilterMode, state.magFilterMode, state.mipmapMode);
      auto sampler = gpu->createSampler(descriptor);
      renderPass->setTexture(textureBinding++, texture, sampler);
    }
    return true;
  }
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
  // The texture set the binders fill, walked from the IR without building a list: the gate runs
  // before a program exists (twice per draw), so it must not allocate.
  auto isTwoD = [](const std::shared_ptr<Texture>& texture) {
    // Only TwoD is supported by the precompiled samplers (see the gate in MatchPermutationImpl for
    // where rectangle and External (OES) textures come from). A slot without a texture counts as
    // "not 2D": the draw would bind a dummy there.
    return texture != nullptr && texture->type() == TextureType::TwoD;
  };
  if (geometryLayout.parameters.atlasProxy != nullptr) {
    auto atlasView = geometryLayout.parameters.atlasProxy->getTextureView();
    if (atlasView != nullptr && !isTwoD(atlasView->getTexture())) {
      return false;
    }
  }
  if (carriedIR != nullptr) {
    for (const auto& proxy : carriedIR->textures) {
      auto view = proxy != nullptr ? proxy->getTextureView() : nullptr;
      if (view == nullptr) {
        // An upload that has not landed binds the dummy, which is a 2D texture.
        continue;
      }
      if (view->isYUV()) {
        auto* yuvView = static_cast<const YUVTextureView*>(view.get());
        for (size_t index = 0; index < yuvView->textureCount(); ++index) {
          if (!isTwoD(yuvView->getTextureAt(index))) {
            return false;
          }
        }
        continue;
      }
      if (!isTwoD(view->getTexture())) {
        return false;
      }
    }
  }
  if (xferFacts.shaderBlend() && xferFacts.dstProxy() != nullptr) {
    auto dstView = xferFacts.dstProxy()->getTextureView();
    if (dstView != nullptr && !isTwoD(dstView->getTexture())) {
      return false;
    }
  }
  return true;
}

}  // namespace tgfx
