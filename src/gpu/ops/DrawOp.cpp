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

#include "gpu/AlignTo.h"
#include "gpu/GlobalCache.h"
#include "gpu/resources/PipelineProgram.h"
#include "inspect/InspectorMark.h"

namespace tgfx {
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

  auto globalCache = renderTarget->getContext()->globalCache();

  auto uboOffsetAlignment =
      static_cast<size_t>(renderTarget->getContext()->gpu()->caps()->shaderCaps()->uboOffsetAlignment);
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
  if (totalUniformBufferSize > 0) {
    size_t lastUniformBufferOffset = 0;
    auto uniformBuffer = globalCache->findOrCreateUniformBuffer(totalUniformBufferSize, &lastUniformBufferOffset);
    if (uniformBuffer != nullptr) {
      auto buffer = static_cast<uint8_t*>(uniformBuffer->map());
      if (vertexUniformData != nullptr) {
        vertexUniformData->setBuffer(buffer + lastUniformBufferOffset);
      }
      if (fragmentUniformData != nullptr) {
        fragmentUniformData->setBuffer(buffer + lastUniformBufferOffset + vertexUniformBufferSize);
      }

      programInfo.setUniformsAndSamplers(renderPass, program.get());

      uniformBuffer->unmap();

      if (vertexUniformData != nullptr) {
        renderPass->setUniformBuffer(VERTEX_UBO_BINDING_POINT, uniformBuffer,
                                   lastUniformBufferOffset, vertexUniformData->size());
        vertexUniformData->setBuffer(nullptr);
      }

      if (fragmentUniformData != nullptr) {
        renderPass->setUniformBuffer(FRAGMENT_UBO_BINDING_POINT, uniformBuffer,
                                   lastUniformBufferOffset + vertexUniformBufferSize, fragmentUniformData->size());
        fragmentUniformData->setBuffer(nullptr);
      }
    }
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
