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
#include "core/utils/Log.h"
#include "gpu/Gpu.h"

namespace tgfx {
static DstTextureInfo CreateDstTextureInfo(RenderPass* renderPass, Rect dstRect) {
  DstTextureInfo dstTextureInfo = {};
  auto context = renderPass->getContext();
  if (context->caps()->textureBarrierSupport && renderPass->renderTargetTexture()) {
    dstTextureInfo.texture = renderPass->renderTargetTexture();
    dstTextureInfo.requiresBarrier = true;
    return dstTextureInfo;
  }
  auto bounds =
      Rect::MakeWH(renderPass->renderTarget()->width(), renderPass->renderTarget()->height());
  if (renderPass->renderTarget()->origin() == ImageOrigin::BottomLeft) {
    auto height = dstRect.height();
    dstRect.top = static_cast<float>(renderPass->renderTarget()->height()) - dstRect.bottom;
    dstRect.bottom = dstRect.top + height;
  }
  if (!dstRect.intersect(bounds)) {
    return {};
  }
  dstRect.roundOut();
  dstTextureInfo.offset = {dstRect.x(), dstRect.y()};
  auto dstTexture = Texture::MakeRGBA(context, static_cast<int>(dstRect.width()),
                                      static_cast<int>(dstRect.height()), false,
                                      renderPass->renderTarget()->origin());
  if (dstTexture == nullptr) {
    LOGE("Failed to create dst texture(%f*%f).", dstRect.width(), dstRect.height());
    return {};
  }
  dstTextureInfo.texture = dstTexture;
  context->gpu()->copyRenderTargetToTexture(renderPass->renderTarget().get(), dstTexture.get(),
                                            dstRect, Point::Zero());
  return dstTextureInfo;
}

std::unique_ptr<Pipeline> DrawOp::createPipeline(RenderPass* renderPass,
                                                 std::unique_ptr<GeometryProcessor> gp) {
  auto numColorProcessors = _colors.size();
  std::vector<std::unique_ptr<FragmentProcessor>> fragmentProcessors = {};
  fragmentProcessors.resize(numColorProcessors + _coverages.size());
  std::move(_colors.begin(), _colors.end(), fragmentProcessors.begin());
  std::move(_coverages.begin(), _coverages.end(),
            fragmentProcessors.begin() + static_cast<int>(numColorProcessors));
  DstTextureInfo dstTextureInfo = {};
  auto caps = renderPass->getContext()->caps();
  if (!BlendModeAsCoeff(blendMode) && !caps->frameBufferFetchSupport) {
    dstTextureInfo = CreateDstTextureInfo(renderPass, bounds());
  }
  auto format = renderPass->renderTarget()->format();
  const auto& swizzle = caps->getWriteSwizzle(format);
  return std::make_unique<Pipeline>(std::move(gp), std::move(fragmentProcessors),
                                    numColorProcessors, blendMode, std::move(dstTextureInfo),
                                    &swizzle);
}

static bool CompareFragments(const std::vector<std::unique_ptr<FragmentProcessor>>& frags1,
                             const std::vector<std::unique_ptr<FragmentProcessor>>& frags2) {
  if (frags1.size() != frags2.size()) {
    return false;
  }
  for (size_t i = 0; i < frags1.size(); i++) {
    if (!frags1[i]->isEqual(*frags2[i])) {
      return false;
    }
  }
  return true;
}

bool DrawOp::onCombineIfPossible(Op* op) {
  auto* that = static_cast<DrawOp*>(op);
  return aa == that->aa && _scissorRect == that->_scissorRect &&
         CompareFragments(_colors, that->_colors) &&
         CompareFragments(_coverages, that->_coverages) && blendMode == that->blendMode;
}

}  // namespace tgfx
