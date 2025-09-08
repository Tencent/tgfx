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
#include "gpu/GlobalCache.h"
#include "gpu/ProgramBuilder.h"
#include "gpu/RenderTarget.h"

namespace tgfx {
ProgramInfo::ProgramInfo(RenderTarget* renderTarget, GeometryProcessor* geometryProcessor,
                         std::vector<FragmentProcessor*> fragmentProcessors,
                         size_t numColorProcessors, XferProcessor* xferProcessor,
                         BlendMode blendMode)
    : renderTarget(renderTarget), geometryProcessor(std::move(geometryProcessor)),
      fragmentProcessors(std::move(fragmentProcessors)), numColorProcessors(numColorProcessors),
      xferProcessor(std::move(xferProcessor)), blendMode(blendMode) {
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

const Swizzle& ProgramInfo::getOutputSwizzle() const {
  auto context = renderTarget->getContext();
  return context->caps()->getWriteSwizzle(renderTarget->format());
}

std::unique_ptr<BlendFormula> ProgramInfo::getBlendFormula() const {
  if (xferProcessor != nullptr) {
    return nullptr;
  }
  auto blendFormula = std::make_unique<BlendFormula>();
  BlendModeAsCoeff(blendMode, numColorProcessors < fragmentProcessors.size(), blendFormula.get());
  return blendFormula;
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
  BytesKey programKey = {};
  geometryProcessor->computeProcessorKey(context, &programKey);
  for (const auto& processor : fragmentProcessors) {
    processor->computeProcessorKey(context, &programKey);
  }
  if (xferProcessor != nullptr) {
    xferProcessor->computeProcessorKey(context, &programKey);
  }
  programKey.write(static_cast<uint32_t>(blendMode));
  programKey.write(static_cast<uint32_t>(getOutputSwizzle().asKey()));
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
}

void ProgramInfo::setUniformsAndSamplers(RenderPass* renderPass, PipelineProgram* program) const {
  DEBUG_ASSERT(renderTarget != nullptr);
  auto vertexUniformBuffer = program->vertexUniformBuffer.get();
  auto fragmentUniformBuffer = program->fragmentUniformBuffer.get();

  auto array = GetRTAdjustArray(renderTarget);
  if (vertexUniformBuffer != nullptr) {
    vertexUniformBuffer->setData(RTAdjustName, array);
  }
  updateUniformBufferSuffix(vertexUniformBuffer, fragmentUniformBuffer, geometryProcessor);

  FragmentProcessor::CoordTransformIter coordTransformIter(this);
  geometryProcessor->setData(vertexUniformBuffer, fragmentUniformBuffer, &coordTransformIter);

  for (auto& fragmentProcessor : fragmentProcessors) {
    FragmentProcessor::Iter iter(fragmentProcessor);
    const FragmentProcessor* fp = iter.next();
    while (fp) {
      updateUniformBufferSuffix(vertexUniformBuffer, fragmentUniformBuffer, fp);
      fp->setData(vertexUniformBuffer, fragmentUniformBuffer);
      fp = iter.next();
    }
  }
  const auto* processor = getXferProcessor();
  updateUniformBufferSuffix(vertexUniformBuffer, fragmentUniformBuffer, processor);
  processor->setData(vertexUniformBuffer, fragmentUniformBuffer);
  updateUniformBufferSuffix(vertexUniformBuffer, fragmentUniformBuffer, nullptr);

  if (vertexUniformBuffer != nullptr) {
    renderPass->setUniformBytes(0, vertexUniformBuffer->data(), vertexUniformBuffer->size());
  }

  if (fragmentUniformBuffer != nullptr) {
    renderPass->setUniformBytes(1, fragmentUniformBuffer->data(), fragmentUniformBuffer->size());
  }

  auto samplers = getSamplers();
  unsigned textureIndex = 0;
  auto gpu = renderTarget->getContext()->gpu();
  for (auto& [texture, state] : samplers) {
    GPUSamplerDescriptor descriptor(ToAddressMode(state.tileModeX), ToAddressMode(state.tileModeY),
                                    state.filterMode, state.filterMode, state.mipmapMode);
    auto sampler = gpu->createSampler(descriptor);
    renderPass->setTexture(textureIndex++, texture, sampler.get());
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

void ProgramInfo::updateUniformBufferSuffix(UniformBuffer* vertexUniformBuffer,
                                            UniformBuffer* fragmentUniformBuffer,
                                            const Processor* processor) const {
  auto suffix = getMangledSuffix(processor);
  if (vertexUniformBuffer != nullptr) {
    vertexUniformBuffer->nameSuffix = suffix;
  }

  if (fragmentUniformBuffer != nullptr) {
    fragmentUniformBuffer->nameSuffix = suffix;
  }
}
}  // namespace tgfx
