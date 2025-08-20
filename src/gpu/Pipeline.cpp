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

#include "Pipeline.h"
#include "gpu/ProgramBuilder.h"
#include "gpu/RenderTarget.h"

namespace tgfx {
Pipeline::Pipeline(PlacementPtr<GeometryProcessor> geometryProcessor,
                   std::vector<PlacementPtr<FragmentProcessor>> fragmentProcessors,
                   size_t numColorProcessors, PlacementPtr<XferProcessor> xferProcessor,
                   BlendMode blendMode, const Swizzle* outputSwizzle)
    : geometryProcessor(std::move(geometryProcessor)),
      fragmentProcessors(std::move(fragmentProcessors)), numColorProcessors(numColorProcessors),
      xferProcessor(std::move(xferProcessor)), blendMode(blendMode), _outputSwizzle(outputSwizzle) {
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

std::unique_ptr<BlendFormula> Pipeline::getBlendFormula() const {
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

void Pipeline::getUniforms(const RenderTarget* renderTarget, UniformBuffer* uniformBuffer) const {
  DEBUG_ASSERT(renderTarget != nullptr);
  DEBUG_ASSERT(uniformBuffer != nullptr);
  auto array = GetRTAdjustArray(renderTarget);
  uniformBuffer->setData(RTAdjustName, array);
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
  processor->setData(uniformBuffer);
  uniformBuffer->nameSuffix = "";
}

std::vector<SamplerInfo> Pipeline::getSamplers() const {
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

void Pipeline::computeProgramKey(Context* context, BytesKey* programKey) const {
  geometryProcessor->computeProcessorKey(context, programKey);
  for (const auto& processor : fragmentProcessors) {
    processor->computeProcessorKey(context, programKey);
  }
  auto dstTextureView = xferProcessor != nullptr ? xferProcessor->dstTextureView() : nullptr;
  if (dstTextureView != nullptr) {
    TextureView::ComputeTextureKey(dstTextureView->getTexture(), programKey);
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
