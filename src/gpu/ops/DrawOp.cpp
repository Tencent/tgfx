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

#include "DrawOp.h"

#include "gpu/GlobalCache.h"
#include "gpu/resources/PipelineProgram.h"
#include "inspect/InspectorMark.h"

namespace tgfx {
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

void DrawOp::execute(RenderPass* renderPass, RenderTarget* renderTarget) {
  OPERATE_MARK(type());
  auto geometryProcessor = onMakeGeometryProcessor(renderTarget);
  ATTRIBUTE_NAME("scissorRect", scissorRect);
  ATTRIBUTE_NAME_ENUM("blenderMode", blendMode, tgfx::inspect::CustomEnumType::BlendMode);
  ATTRIBUTE_NAME_ENUM("aaType", aaType, tgfx::inspect::CustomEnumType::AAType);
  if (geometryProcessor == nullptr) {
    return;
  }
  std::vector<FragmentProcessor*> fragmentProcessors = {};
  fragmentProcessors.reserve(colors.size() + coverages.size());
  for (auto& color : colors) {
    fragmentProcessors.emplace_back(color.get());
  }
  for (auto& coverage : coverages) {
    fragmentProcessors.emplace_back(coverage.get());
  }
  ProgramInfo programInfo(renderTarget, geometryProcessor.get(), std::move(fragmentProcessors),
                          colors.size(), xferProcessor.get(), blendMode);
  auto program = std::static_pointer_cast<PipelineProgram>(programInfo.getProgram());
  if (program == nullptr) {
    LOGE("DrawOp::execute() Failed to get the program!");
    return;
  }
  renderPass->setPipeline(program->getPipeline());

  auto context = renderTarget->getContext();
  auto vertexUniformData = program->getUniformData(ShaderStage::Vertex);
  auto fragmentUniformData = program->getUniformData(ShaderStage::Fragment);
  std::shared_ptr<GPUBuffer> vertexUniformBuffer = nullptr;
  std::shared_ptr<GPUBuffer> fragmentUniformBuffer = nullptr;
  size_t vertexUniformBufferOffset = 0;
  size_t fragmentUniformBufferOffset = 0;
  if (vertexUniformData != nullptr && vertexUniformData->size() > 0) {
    vertexUniformBuffer = context->globalCache()->findOrCreateUniformBuffer(vertexUniformData->size(), &vertexUniformBufferOffset);
    if (vertexUniformBuffer != nullptr) {
      vertexUniformData->setBuffer(static_cast<uint8_t*>(vertexUniformBuffer->map()) + vertexUniformBufferOffset);
    }
  }
  if (fragmentUniformData != nullptr && fragmentUniformData->size() > 0) {
    fragmentUniformBuffer = context->globalCache()->findOrCreateUniformBuffer(fragmentUniformData->size(), &fragmentUniformBufferOffset);
    if (fragmentUniformBuffer != nullptr) {
      fragmentUniformData->setBuffer(static_cast<uint8_t*>(fragmentUniformBuffer->map()) + fragmentUniformBufferOffset);
    }
  }
  programInfo.getUniformData(vertexUniformData, fragmentUniformData);

  if (vertexUniformData != nullptr && vertexUniformBuffer != nullptr) {
    vertexUniformBuffer->unmap();

    renderPass->setUniformBuffer(VERTEX_UBO_BINDING_POINT, vertexUniformBuffer, vertexUniformBufferOffset, vertexUniformData->size());
  }

  if (fragmentUniformData != nullptr && fragmentUniformBuffer != nullptr) {
    fragmentUniformBuffer->unmap();

    renderPass->setUniformBuffer(FRAGMENT_UBO_BINDING_POINT, fragmentUniformBuffer, fragmentUniformBufferOffset, fragmentUniformData->size());
  }

  auto gpu = renderTarget->getContext()->gpu();
  auto samplers = programInfo.getSamplers();
  unsigned textureBinding = TEXTURE_BINDING_POINT_START;
  for (auto& [texture, state] : samplers) {
    GPUSamplerDescriptor descriptor(ToAddressMode(state.tileModeX), ToAddressMode(state.tileModeY),
                                    state.filterMode, state.filterMode, state.mipmapMode);
    auto sampler = gpu->createSampler(descriptor);
    renderPass->setTexture(textureBinding++, texture, sampler);
  }

  if (scissorRect.isEmpty()) {
    renderPass->setScissorRect(0, 0, renderTarget->width(), renderTarget->height());
  } else {
    renderPass->setScissorRect(static_cast<int>(scissorRect.x()), static_cast<int>(scissorRect.y()),
                               static_cast<int>(scissorRect.width()),
                               static_cast<int>(scissorRect.height()));
  }
  onDraw(renderPass);
  CAPUTRE_FRARGMENT_PROCESSORS(renderTarget->getContext(), colors, coverages);
  CAPUTRE_RENDER_TARGET(renderTarget);
}
}  // namespace tgfx
