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
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include "AlignTo.h"
#include "gpu/BlendFormula.h"
#include "gpu/GlobalCache.h"
#include "gpu/PrecompiledProgramCreator.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/ProgramBuilder.h"
#include "gpu/resources/RenderTarget.h"
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
  if (!BlendModeAsCoeff(blendMode, numColorProcessors < fragmentProcessors.size(), &blendFormula)) {
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

static std::array<float, 4> GetRTAdjustArray(const RenderTarget* renderTarget) {
  std::array<float, 4> result = {};
  result[0] = 2.f / static_cast<float>(renderTarget->width());
  result[2] = 2.f / static_cast<float>(renderTarget->height());
  result[1] = -1.f;
  result[3] = -1.f;
  if (renderTarget->origin() == ImageOrigin::BottomLeft) {
    result[2] = -result[2];
    result[3] = -result[3];
  }
  // WebGPU viewport Y transform is (1 - y_ndc)/2, meaning NDC y=+1 maps to framebuffer top.
  // For TopLeft origin (pixel y=0 at top), we need pixel y=0 -> NDC y=+1, requiring Y negate.
  if (renderTarget->getContext()->backend() == Backend::WebGPU &&
      renderTarget->origin() == ImageOrigin::TopLeft) {
    result[2] = -result[2];
    result[3] = -result[3];
  }
  return result;
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

static std::string DiagnosticProgramKey(const BytesKey& key) {
  std::stringstream stream;
  stream << key.size() << ":" << std::hex << std::setfill('0');
  for (size_t index = 0; index < key.size(); ++index) {
    stream << std::setw(8) << key.data()[index];
  }
  return stream.str();
}

BytesKey ProgramInfo::programKeyForDiagnostics() const {
  return buildProgramKey();
}

BytesKey ProgramInfo::buildProgramKey() const {
  BytesKey key = {};
  auto context = renderTarget->getContext();
  geometryProcessor->computeProcessorKey(context, &key);
  for (const auto& processor : fragmentProcessors) {
    processor->computeProcessorKey(context, &key);
  }
  if (xferProcessor != nullptr) {
    xferProcessor->computeProcessorKey(context, &key);
  }
  key.write(static_cast<uint32_t>(blendMode));
  key.write(static_cast<uint32_t>(getOutputSwizzle().asKey()));
  key.write(static_cast<uint32_t>(cullMode));
  key.write(static_cast<uint32_t>(renderTarget->format()));
  key.write(static_cast<uint32_t>(renderTarget->sampleCount()));
  key.write(colorWriteMask);
  key.write(static_cast<uint32_t>(depthStencil.format));
  key.write(static_cast<uint32_t>(depthStencil.depthCompare));
  key.write(static_cast<uint32_t>(depthStencil.depthWriteEnabled ? 1 : 0));
  key.write(depthStencil.stencilReadMask);
  key.write(depthStencil.stencilWriteMask);
  EncodeStencilFace(key, depthStencil.stencilFront);
  EncodeStencilFace(key, depthStencil.stencilBack);
  return key;
}

std::shared_ptr<Program> ProgramInfo::getProgram(ProgramLookupMode mode) const {
  auto context = renderTarget->getContext();
  auto globalCache = context->globalCache();

  auto programKey = buildProgramKey();
  auto program = globalCache->findProgram(programKey);
  bool cacheKeyOccupied = program != nullptr;
  bool programCreated = false;
  if (program != nullptr && mode == ProgramLookupMode::PrecompiledOnly &&
      program->getProvenance().program != ProgramOrigin::PrecompiledArtifact) {
    program = nullptr;
  }
  if (program == nullptr) {
    program = PrecompiledProgramCreator::CreateProgram(context, this);
    if (program == nullptr && mode == ProgramLookupMode::AllowRuntimeFallback) {
      program = ProgramBuilder::CreateProgram(context, this);
    }
    programCreated = program != nullptr;
    if (program == nullptr) {
      LOGE("ProgramInfo::getProgram() Failed to create the program!");
      return nullptr;
    }
    if (programCreated && context->precompiledShaderCache()->diagnosticRecordingEnabled() &&
        program->getProvenance().program != ProgramOrigin::PrecompiledArtifact) {
      context->precompiledShaderCache()->recordJITProgram({DiagnosticProgramKey(programKey)});
    }
  }
  if (mode == ProgramLookupMode::PrecompiledOnly &&
      program->getProvenance().program != ProgramOrigin::PrecompiledArtifact) {
    LOGE("ProgramInfo::getProgram() Rejected a non-precompiled program!");
    return nullptr;
  }
  // Lookup mode is intentionally not part of the key: a precompiled program is valid for both
  // modes. If a runtime program already occupies the key, PrecompiledOnly may return a newly created
  // artifact for this lookup but must not replace that cache entry and corrupt its LRU bookkeeping.
  if (programCreated && !cacheKeyOccupied) {
    globalCache->addProgram(programKey, program);
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

static AddressMode ToAddressMode(TileMode tileMode) {
  switch (tileMode) {
    case TileMode::Clamp:
      return AddressMode::ClampToEdge;
    case TileMode::Repeat:
      return AddressMode::Repeat;
    case TileMode::Mirror:
      return AddressMode::MirrorRepeat;
    case TileMode::Decal:
      return AddressMode::ClampToBorder;
  }
  return AddressMode::ClampToEdge;
}

void ProgramInfo::setUniformsAndSamplers(RenderPass* renderPass, Program* program) const {
  DEBUG_ASSERT(renderTarget != nullptr);
  size_t vertexOffset = 0;
  size_t fragmentOffset = 0;
  auto uniformBuffer = getUniformBuffer(program, &vertexOffset, &fragmentOffset);

  auto vertexUniformData = program->getUniformData(ShaderStage::Vertex);
  auto fragmentUniformData = program->getUniformData(ShaderStage::Fragment);

  // UniformData is reused across draws, so clear the per-texture structural ordinal left over from a
  // previous draw before writing the base-name uniforms below (RTAdjust / OutputAlphaSwizzle). A
  // stale suffix would misdirect those writes (e.g. tgfx_RTAdjust_1) on the precompiled path.
  if (vertexUniformData != nullptr) {
    vertexUniformData->structuralSuffix = "";
  }
  if (fragmentUniformData != nullptr) {
    fragmentUniformData->structuralSuffix = "";
  }

  auto array = GetRTAdjustArray(renderTarget);
  if (vertexUniformData != nullptr) {
    vertexUniformData->setData(RTAdjustName, array);
  }
  if (fragmentUniformData != nullptr) {
    // Precompiled fill kernels apply the render-target write swizzle at output via this uniform.
    // Only the alpha-only (AAAA) case is non-identity; RGBA targets leave it 0 (no-op). The
    // runtime-generated shader bakes the swizzle into code and does not declare it, so set it
    // optionally. Set in the base (unmangled) context, like RTAdjust.
    int outputAlphaSwizzle = getOutputSwizzle() == Swizzle::AAAA() ? 1 : 0;
    fragmentUniformData->setDataOptional("OutputAlphaSwizzle", outputAlphaSwizzle);
    fragmentUniformData->setDataOptional("HasClip", 0);
  }
  updateUniformDataSuffix(vertexUniformData, fragmentUniformData, geometryProcessor);

  FragmentProcessor::CoordTransformIter coordTransformIter(this);
  geometryProcessor->setData(vertexUniformData, fragmentUniformData, &coordTransformIter);

  // Per-processor structural ordinal: on the precompiled path the runtime processor suffix is
  // stripped (skipSuffix), so multiple processors of the same class would write the same base-named
  // per-instance uniforms (a TextureEffect's Subset/AlphaOnly, an XfermodeFragmentProcessor's
  // BlendModeValue, a ConstColorProcessor's ConstColor, ...) and clobber one another. Numbering each
  // processor by its index among same-class processors (in traversal order) gives the precompiled
  // shader stable, predictable names to address them by (Subset / Subset_1, BlendModeValue /
  // BlendModeValue_1, ...). The first processor of each class keeps the base name, so kernels with
  // at most one instance of a class are byte-for-byte unchanged. On the JIT path structuralSuffix is
  // unused (nameSuffix already disambiguates by processor index).
  std::unordered_map<std::string, int> classOrdinals;
  for (auto& fragmentProcessor : fragmentProcessors) {
    FragmentProcessor::Iter iter(fragmentProcessor);
    const FragmentProcessor* fp = iter.next();
    while (fp) {
      updateUniformDataSuffix(vertexUniformData, fragmentUniformData, fp);
      int ordinal = classOrdinals[fp->name()]++;
      std::string structural = ordinal > 0 ? "_" + std::to_string(ordinal) : "";
      if (vertexUniformData != nullptr) {
        vertexUniformData->structuralSuffix = structural;
      }
      if (fragmentUniformData != nullptr) {
        fragmentUniformData->structuralSuffix = structural;
      }
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
  auto expected = program->expectedTextureCount();
  // The dst texture always binds last in the artifact. When the artifact declares more samplers
  // than the draw supplies (e.g. an absent device mask on a runtime-guarded kernel), the holes sit
  // before the dst, so hold the dst aside and pad with the shared dummy texture first; the guarded
  // shader path never samples the padded slots.
  bool holdDst = expected > samplers.size() && !samplers.empty() && xferProcessor != nullptr &&
                 xferProcessor->dstTextureView() != nullptr;
  size_t sequentialCount = holdDst ? samplers.size() - 1 : samplers.size();
  // The device-mask slot in precompiled kernels sits immediately after the color-side textures
  // (geometry-processor textures plus the color fragment-processor subtrees); the dummy for an
  // absent device mask must occupy that slot, pushing local-mask textures right.
  size_t colorSamplerCount = geometryProcessor->numTextureSamplers();
  for (size_t i = 0; i < numColorFragmentProcessors(); ++i) {
    FragmentProcessor::Iter colorIter(fragmentProcessors[i]);
    const FragmentProcessor* cfp = colorIter.next();
    while (cfp) {
      colorSamplerCount += cfp->numTextureSamplers();
      cfp = colorIter.next();
    }
  }
  unsigned textureBinding = 0;
  auto gpu = renderTarget->getContext()->gpu();
  auto* cache = renderTarget->getContext()->globalCache();
  // The device-mask slot is the only hole a draw can leave in a precompiled kernel's sampler
  // list, so an expected/supplied gap means the dummy must be inserted there.
  bool padHole = expected > samplers.size();
  for (size_t i = 0; i < sequentialCount; ++i) {
    if (padHole && textureBinding == colorSamplerCount) {
      renderPass->setTexture(textureBinding++, cache->getOrCreateDummyTexture(),
                             cache->getOrCreateDummySampler());
    }
    auto& [texture, state] = samplers[i];
    SamplerDescriptor descriptor(ToAddressMode(state.tileModeX), ToAddressMode(state.tileModeY),
                                 state.minFilterMode, state.magFilterMode, state.mipmapMode);
    auto sampler = gpu->createSampler(descriptor);
    renderPass->setTexture(textureBinding++, texture, sampler);
  }
  if (textureBinding < expected - (holdDst ? 1u : 0u)) {
    auto dummyTexture = cache->getOrCreateDummyTexture();
    auto dummySampler = cache->getOrCreateDummySampler();
    unsigned stop = expected - (holdDst ? 1u : 0u);
    while (textureBinding < stop) {
      renderPass->setTexture(textureBinding++, dummyTexture, dummySampler);
    }
  }
  if (holdDst) {
    auto& [texture, state] = samplers.back();
    SamplerDescriptor descriptor(ToAddressMode(state.tileModeX), ToAddressMode(state.tileModeY),
                                 state.minFilterMode, state.magFilterMode, state.mipmapMode);
    auto sampler = gpu->createSampler(descriptor);
    renderPass->setTexture(textureBinding, texture, sampler);
  }
}

Backend ProgramInfo::backend() const {
  return renderTarget->getContext()->backend();
}

bool ProgramInfo::usesOpenGLDesktopAOTProfile() const {
  auto shaderCaps = renderTarget->getContext()->shaderCaps();
  return !shaderCaps->usesPrecisionModifiers && shaderCaps->versionDeclString == "#version 150";
}

bool ProgramInfo::samplersAre2D() const {
  for (const auto& sampler : getSamplers()) {
    if (sampler.texture == nullptr) {
      return false;
    }
    // Only TwoD is supported by the precompiled samplers. External (OES) and rectangle textures
    // (external adopters only) keep the ProgramBuilder route.
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

void ProgramInfo::updateUniformDataSuffix(UniformData* vertexUniformData,
                                          UniformData* fragmentUniformData,
                                          const Processor* processor) const {
  auto suffix = getMangledSuffix(processor);
  if (vertexUniformData != nullptr) {
    vertexUniformData->nameSuffix = suffix;
    // Cleared by default; the fragment-processor loop reassigns it per TextureEffect. This keeps the
    // geometry processor and xfer processor (and any non-texture FP) on the base uniform names.
    vertexUniformData->structuralSuffix = "";
  }

  if (fragmentUniformData != nullptr) {
    fragmentUniformData->nameSuffix = suffix;
    fragmentUniformData->structuralSuffix = "";
  }
}
}  // namespace tgfx
