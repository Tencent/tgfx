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
#include "gpu/GlobalCache.h"
#include "gpu/ProgramBuilder.h"
#include "gpu/ShaderBindingLayout.h"
#include "gpu/ShaderCaps.h"
#include "gpu/processors/EmptyXferProcessor.h"
#include "gpu/processors/GeometryProcessor.h"
#include "gpu/processors/XferProcessor.h"
#include "gpu/resources/RenderTarget.h"
#include "gpu/resources/YUVTextureView.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {

// The render-target adjust vector (2/width, -1, ±2/height, ±1): the vertex stage's uniform.
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
  // WebGPU's viewport maps NDC y=+1 to the framebuffer top; a top-left target needs the negate.
  if (renderTarget->getContext()->backend() == Backend::WebGPU &&
      renderTarget->origin() == ImageOrigin::TopLeft) {
    result[2] = -result[2];
    result[3] = -result[3];
  }
  return result;
}

// The sampler address mode a tile mode maps onto.
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
      xferProcessor(xferProcessor), blendMode(blendMode),
      hasCoverageForBlend(numColorProcessors < this->fragmentProcessors.size()) {
  updateProcessorIndices();
}

const std::vector<Attribute>& ProgramInfo::getVertexAttributes() const {
  static const std::vector<Attribute> empty = {};
  return geometryProcessor != nullptr ? geometryProcessor->vertexAttributes() : empty;
}

const std::vector<Attribute>& ProgramInfo::getInstanceAttributes() const {
  static const std::vector<Attribute> empty = {};
  return geometryProcessor != nullptr ? geometryProcessor->instanceAttributes() : empty;
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
  // A bound xfer processor runs the blend formula in the shader, so the fixed-function blend must
  // stay off there too — porter-duff modes such as SrcIn have no fixed-function equivalent.
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

std::string ProgramInfo::FormatDiagnosticKey(const BytesKey& key) {
  std::stringstream stream;
  stream << key.size() << ":" << std::hex << std::setfill('0');
  for (size_t index = 0; index < key.size(); ++index) {
    stream << std::setw(8) << key.data()[index];
  }
  return stream.str();
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
  // A draw without a geometry processor has no program source.
  LOGE("ProgramInfo::setUniformsAndSamplers() the info carries no geometry processor");
  return false;
}

Backend ProgramInfo::backend() const {
  return renderTarget->getContext()->backend();
}

}  // namespace tgfx
