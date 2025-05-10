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

#include "DrawOp.h"
#include "gpu/processors/PorterDuffXferProcessor.h"

namespace tgfx {
PlacementPtr<Pipeline> DrawOp::createPipeline(RenderPass* renderPass,
                                              PlacementPtr<GeometryProcessor> gp) {
  auto numColorProcessors = colors.size();
  auto fragmentProcessors = std::move(colors);
  fragmentProcessors.reserve(numColorProcessors + coverages.size());

  for (auto& coverage : coverages) {
    fragmentProcessors.emplace_back(std::move(coverage));
  }
  auto format = renderPass->renderTarget()->format();
  auto context = renderPass->getContext();
  const auto& swizzle = context->caps()->getWriteSwizzle(format);
  PlacementPtr<XferProcessor> xferProcessor = nullptr;
  if (BlendModeNeedDesTexture(blendMode, !coverages.empty())) {
    auto dstTextureInfo = makeDstTextureInfo(renderPass);
    if (!context->caps()->frameBufferFetchSupport && !dstTextureInfo.texture) {
      return nullptr;
    }
    xferProcessor = PorterDuffXferProcessor::Make(context->drawingBuffer(), blendMode,
                                                  std::move(dstTextureInfo));
  }
  return context->drawingBuffer()->make<Pipeline>(std::move(gp), std::move(fragmentProcessors),
                                                  numColorProcessors, std::move(xferProcessor),
                                                  blendMode, &swizzle);
}

DstTextureInfo DrawOp::makeDstTextureInfo(RenderPass* renderPass) {
  auto context = renderPass->getContext();
  auto caps = context->caps();
  if (caps->frameBufferFetchSupport) {
    return {};
  }
  auto renderTarget = renderPass->renderTarget();
  auto texture = caps->textureBarrierSupport ? renderPass->renderTargetTexture() : nullptr;

  DstTextureInfo dstTextureInfo = {};

  if (texture != nullptr) {
    if (renderTarget->sampleCount() > 1) {
      renderPass->resolve(deviceBounds);
    }
    dstTextureInfo.texture = std::move(texture);
    dstTextureInfo.requiresBarrier = true;
    return dstTextureInfo;
  }
  dstTextureInfo.offset = {deviceBounds.x(), deviceBounds.y()};

  texture = Texture::MakeFormat(context, static_cast<int>(deviceBounds.width()),
                                static_cast<int>(deviceBounds.height()), renderTarget->format(),
                                false, renderTarget->origin());
  if (!texture) {
    return {};
  }
  renderPass->copyToTexture(texture.get(), static_cast<int>(deviceBounds.x()),
                            static_cast<int>(deviceBounds.y()));
  dstTextureInfo.texture = std::move(texture);
  return dstTextureInfo;
}
}  // namespace tgfx
