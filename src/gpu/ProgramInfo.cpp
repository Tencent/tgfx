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
#include "gpu/ProgramBuilder.h"
#include "gpu/resources/RenderTarget.h"
#include "opengl/FakeUniformBuffer.h"

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

  auto pipelineProgram = std::static_pointer_cast<PipelineProgram>(program);
  if (pipelineProgram != nullptr) {
    if (context->caps()->shaderCaps()->uboSupport) {
      size_t uniformBufferSize = 0;
      if (pipelineProgram->vertexUniformData != nullptr) {
        uniformBufferSize +=
            AlignTo(pipelineProgram->vertexUniformData->size(),
                    static_cast<size_t>(context->caps()->shaderCaps()->uboOffsetAlignment));
      }
      if (pipelineProgram->fragmentUniformData != nullptr) {
        uniformBufferSize += pipelineProgram->fragmentUniformData->size();
      }
      if (uniformBufferSize > 0) {
        pipelineProgram->uniformBuffer = context->globalCache()->findOrCreateUniformBuffer(
            uniformBufferSize, &pipelineProgram->uniformBufferBaseOffset, false);
      }
    } else {
      size_t uniformBufferSize = 0;
      if (pipelineProgram->vertexUniformData != nullptr) {
        uniformBufferSize += pipelineProgram->vertexUniformData->size();
      }
      if (pipelineProgram->fragmentUniformData != nullptr) {
        uniformBufferSize += pipelineProgram->fragmentUniformData->size();
      }
      if (uniformBufferSize > 0) {
        pipelineProgram->uniformBuffer = context->globalCache()->findOrCreateUniformBuffer(
            uniformBufferSize, &pipelineProgram->uniformBufferBaseOffset, true);
      }
      pipelineProgram->uniformBufferBaseOffset = 0;
    }
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
  auto vertexUniformData = program->vertexUniformData.get();
  auto fragmentUniformData = program->fragmentUniformData.get();

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

  auto gpu = renderTarget->getContext()->gpu();
  if (gpu->caps()->shaderCaps()->uboSupport) {
    size_t fragmentUniformOffset = 0;
    if (vertexUniformData != nullptr) {
      auto buffer = program->uniformBuffer->map(gpu, program->uniformBufferBaseOffset,
                                                vertexUniformData->size());
      if (buffer != nullptr) {
        memcpy(buffer, vertexUniformData->data(), vertexUniformData->size());
      }
      program->uniformBuffer->unmap(gpu);

      renderPass->setUniformBuffer(VERTEX_UBO_BINDING_POINT, program->uniformBuffer,
                                   program->uniformBufferBaseOffset, vertexUniformData->size());
      fragmentUniformOffset =
          AlignTo(vertexUniformData->size(),
                  static_cast<size_t>(gpu->caps()->shaderCaps()->uboOffsetAlignment));
    }

    if (fragmentUniformData != nullptr) {
      auto buffer =
          program->uniformBuffer->map(gpu, program->uniformBufferBaseOffset + fragmentUniformOffset,
                                      fragmentUniformData->size());
      if (buffer != nullptr) {
        memcpy(buffer, fragmentUniformData->data(), fragmentUniformData->size());
      }
      program->uniformBuffer->unmap(gpu);

      renderPass->setUniformBuffer(FRAGMENT_UBO_BINDING_POINT, program->uniformBuffer,
                                   program->uniformBufferBaseOffset + fragmentUniformOffset,
                                   fragmentUniformData->size());
    }
  } else {
    size_t fragmentUniformOffset = 0;
    if (vertexUniformData != nullptr) {
      auto buffer = program->uniformBuffer->map(gpu, program->uniformBufferBaseOffset,
                                                vertexUniformData->size());
      if (buffer != nullptr) {
        memcpy(buffer, vertexUniformData->data(), vertexUniformData->size());
      }
      program->uniformBuffer->unmap(gpu);

      renderPass->setUniformBuffer(VERTEX_UBO_BINDING_POINT, program->uniformBuffer,
                                   program->uniformBufferBaseOffset, vertexUniformData->size());
      fragmentUniformOffset = vertexUniformData->size();
    }
    if (fragmentUniformData != nullptr) {
      auto buffer =
          program->uniformBuffer->map(gpu, program->uniformBufferBaseOffset + fragmentUniformOffset,
                                      fragmentUniformData->size());
      if (buffer != nullptr) {
        memcpy(buffer, fragmentUniformData->data(), fragmentUniformData->size());
      }
      program->uniformBuffer->unmap(gpu);
      renderPass->setUniformBuffer(FRAGMENT_UBO_BINDING_POINT, program->uniformBuffer,
                                   program->uniformBufferBaseOffset + fragmentUniformOffset,
                                   fragmentUniformData->size());
    }
  }

  auto samplers = getSamplers();
  unsigned textureBinding = TEXTURE_BINDING_POINT_START;
  for (auto& [texture, state] : samplers) {
    GPUSamplerDescriptor descriptor(ToAddressMode(state.tileModeX), ToAddressMode(state.tileModeY),
                                    state.filterMode, state.filterMode, state.mipmapMode);
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
