/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "Pipeline.h"
#include "gpu/ProgramBuilder.h"
#include "gpu/TextureSampler.h"
#include "gpu/processors/PorterDuffXferProcessor.h"

namespace tgfx {
Pipeline::Pipeline(std::unique_ptr<GeometryProcessor> geometryProcessor,
                   std::vector<std::unique_ptr<FragmentProcessor>> fragmentProcessors,
                   size_t numColorProcessors, BlendMode blendMode, DstTextureInfo dstTextureInfo,
                   const Swizzle* outputSwizzle)
    : geometryProcessor(std::move(geometryProcessor)),
      fragmentProcessors(std::move(fragmentProcessors)), numColorProcessors(numColorProcessors),
      dstTextureInfo(std::move(dstTextureInfo)), _outputSwizzle(outputSwizzle) {
  if (!BlendModeAsCoeff(blendMode, &_blendInfo)) {
    xferProcessor = PorterDuffXferProcessor::Make(blendMode);
  }
  updateProcessorIndices();
}

void Pipeline::updateProcessorIndices() {
  int index = 0;
  processorIndices[geometryProcessor.get()] = index++;
  for (auto& fragmentProcessor : fragmentProcessors) {
    FragmentProcessor::Iter iter(fragmentProcessor.get());
    const FragmentProcessor* fp = iter.next();
    while (fp) {
      processorIndices[fp] = index++;
      fp = iter.next();
    }
  }
  processorIndices[getXferProcessor()] = index++;
}

const XferProcessor* Pipeline::getXferProcessor() const {
  if (xferProcessor == nullptr) {
    return EmptyXferProcessor::GetInstance();
  }
  return xferProcessor.get();
}

void Pipeline::getUniforms(UniformBuffer* uniformBuffer) const {
  uniformBuffer->nameSuffix = getMangledSuffix(geometryProcessor.get());
  FragmentProcessor::CoordTransformIter coordTransformIter(this);
  geometryProcessor->setData(uniformBuffer, &coordTransformIter);
  for (auto& fragmentProcessor : fragmentProcessors) {
    FragmentProcessor::Iter iter(fragmentProcessor.get());
    const FragmentProcessor* fp = iter.next();
    while (fp) {
      uniformBuffer->nameSuffix = getMangledSuffix(fp);
      fp->setData(uniformBuffer);
      fp = iter.next();
    }
  }
  auto processor = getXferProcessor();
  uniformBuffer->nameSuffix = getMangledSuffix(processor);
  auto texture = dstTextureInfo.texture ? dstTextureInfo.texture.get() : nullptr;
  processor->setData(uniformBuffer, texture, dstTextureInfo.offset);
  uniformBuffer->nameSuffix = "";
}

std::vector<SamplerInfo> Pipeline::getSamplers() const {
  std::vector<SamplerInfo> samplers = {};
  FragmentProcessor::Iter iter(this);
  const FragmentProcessor* fp = iter.next();
  while (fp) {
    for (size_t i = 0; i < fp->numTextureSamplers(); ++i) {
      SamplerInfo sampler = {fp->textureSampler(i), fp->samplerState(i)};
      samplers.push_back(sampler);
    }
    fp = iter.next();
  }
  if (dstTextureInfo.texture != nullptr) {
    SamplerInfo sampler = {dstTextureInfo.texture->getSampler(), {}};
    samplers.push_back(sampler);
  }
  return samplers;
}

void Pipeline::computeProgramKey(Context* context, BytesKey* programKey) const {
  geometryProcessor->computeProcessorKey(context, programKey);
  if (dstTextureInfo.texture != nullptr) {
    dstTextureInfo.texture->getSampler()->computeKey(context, programKey);
  }
  for (const auto& processor : fragmentProcessors) {
    processor->computeProcessorKey(context, programKey);
  }
  getXferProcessor()->computeProcessorKey(context, programKey);
  programKey->write(static_cast<uint32_t>(_outputSwizzle->asKey()));
}

std::unique_ptr<Program> Pipeline::createProgram(Context* context) const {
  return ProgramBuilder::CreateProgram(context, this);
}

int Pipeline::getProcessorIndex(const Processor* processor) const {
  auto result = processorIndices.find(processor);
  if (result == processorIndices.end()) {
    return -1;
  }
  return result->second;
}

std::string Pipeline::getMangledSuffix(const Processor* processor) const {
  auto processorIndex = getProcessorIndex(processor);
  if (processorIndex == -1) {
    return "";
  }
  return "_P" + std::to_string(processorIndex);
}
}  // namespace tgfx
