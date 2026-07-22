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

std::shared_ptr<Program> ProgramInfo::getProgram() const {
  auto context = renderTarget->getContext();
  // Builds the program cache key for a given route. The route is the last field written so that a
  // decomposed program (which binds an Opcode uniform layout) never collides with a plain program
  // sharing the same processors and pipeline state. Keep the non-route fields in sync with the
  // fields actually written into RenderPipelineDescriptor.
  auto buildKey = [this, context](AOTDecompositionRoute route) {
    BytesKey key = {};
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
    key.write(static_cast<uint32_t>(route));
    return key;
  };

  // Safety fallback contract (review #2): program creation is atomic and never caches a program
  // whose uniform layout disagrees with the route it is stored under. `route` is a local copy — the
  // const ProgramInfo is never mutated. When this draw opted into a decomposed route we try the
  // decomposed creator first; if it yields nullptr (unsupported chain, invalid layout, or pipeline
  // failure) we drop to the plain route entirely: recompute the key with route None and use the
  // plain matcher/builder, so the plain program is cached only under the plain key. Correctness of a
  // successfully created decomposed program is proven by offline cross-validation, not a runtime
  // pixel check.
  auto route = decompositionRoute;
  auto programKey = buildKey(route);
  auto program = context->globalCache()->findProgram(programKey);
  if (program == nullptr) {
    if (route != AOTDecompositionRoute::None) {
      program = PrecompiledProgramCreator::CreateDecomposedProgram(context, this);
      if (program == nullptr) {
        route = AOTDecompositionRoute::None;
        programKey = buildKey(route);
        program = context->globalCache()->findProgram(programKey);
      }
    }
    if (program == nullptr) {
      program = PrecompiledProgramCreator::CreateProgram(context, this);
      if (program == nullptr) {
        program = ProgramBuilder::CreateProgram(context, this);
      }
      if (program == nullptr) {
        LOGE("ProgramInfo::getProgram() Failed to create the program!");
        return nullptr;
      }
    }
    context->globalCache()->addProgram(programKey, program);
  }
  // Draw-level metric baseline point: each getProgram() call is one pass of one logical Draw. In
  // the current single-pass world one Draw == one Kernel invocation. A Draw counts as a complete
  // AOT Draw only when its program originated from a precompiled artifact rather than the
  // ProgramBuilder fallback. The decomposition executor (stage 2+) will extend the delta with
  // materialized edges and offscreen targets.
  //
  // Gated behind diagnostic recording so the production hot path stays free of the recordDraw lock:
  // Draw metrics are only consumed during test/diagnostic runs (the reporter enables recording per
  // test), so a disabled-by-default atomic check is all a normal draw pays here.
  auto cache = context->precompiledShaderCache();
  if (cache->diagnosticRecordingEnabled()) {
    AOTDrawStats drawDelta = {};
    drawDelta.kernelInvocations = 1;
    bool completeAOTDraw = program->getProvenance().program == ProgramOrigin::PrecompiledArtifact;
    cache->recordDraw(drawDelta, completeAOTDraw);
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
  }

  if (fragmentUniformData != nullptr) {
    fragmentUniformData->nameSuffix = suffix;
  }
}
}  // namespace tgfx
