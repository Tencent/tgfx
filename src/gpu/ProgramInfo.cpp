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
#include "gpu/BlendFormula.h"
#include "gpu/GlobalCache.h"
#include "gpu/ProgramBuilder.h"
#include "gpu/resources/RenderTarget.h"

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

PipelineColorAttachment ProgramInfo::getPipelineColorAttachment() const {
  PipelineColorAttachment colorAttachment = {};
  colorAttachment.format = renderTarget->format();
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
  colorAttachment.colorWriteMask = ColorWriteMask::All;
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

void ProgramInfo::getUniformData(UniformData* vertexUniformData,
                                 UniformData* fragmentUniformData) const {
  DEBUG_ASSERT(renderTarget != nullptr);

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
  const auto* processor = getXferProcessor();
  updateUniformDataSuffix(vertexUniformData, fragmentUniformData, processor);
  processor->setData(vertexUniformData, fragmentUniformData);
  updateUniformDataSuffix(vertexUniformData, fragmentUniformData, nullptr);
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
