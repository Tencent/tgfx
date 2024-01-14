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

#include "TextureShape.h"
#include "gpu/ProxyProvider.h"
#include "gpu/ops/FillRectOp.h"
#include "gpu/processors/TextureEffect.h"
#include "tgfx/core/Mask.h"

namespace tgfx {
TextureShape::TextureShape(std::shared_ptr<PathProxy> pathProxy, float resolutionScale)
    : PathShape(std::move(pathProxy), resolutionScale) {
  resourceKey = ResourceKey::NewWeak();
  auto path = getFillPath();
  auto width = ceilf(bounds.width());
  auto height = ceilf(bounds.height());
  auto size = ISize::Make(width, height);
  auto matrix = Matrix::MakeTrans(-bounds.x(), -bounds.y());
  matrix.postScale(width / bounds.width(), height / bounds.height());
  rasterizer = Rasterizer::MakeFrom(path, size, matrix);
}

std::unique_ptr<DrawOp> TextureShape::makeOp(GpuPaint* paint, const Matrix& viewMatrix,
                                             uint32_t renderFlags) const {
  auto proxyProvider = paint->context->proxyProvider();
  auto textureProxy =
      proxyProvider->createTextureProxy(resourceKey, rasterizer, false, renderFlags);
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto maskLocalMatrix = Matrix::I();
  maskLocalMatrix.postTranslate(-bounds.x(), -bounds.y());
  maskLocalMatrix.postScale(static_cast<float>(textureProxy->width()) / bounds.width(),
                            static_cast<float>(textureProxy->height()) / bounds.height());
  paint->coverageFragmentProcessors.emplace_back(FragmentProcessor::MulInputByChildAlpha(
      TextureEffect::Make(std::move(textureProxy), SamplingOptions(), &maskLocalMatrix)));
  return FillRectOp::Make(paint->color, bounds, viewMatrix);
}
}  // namespace tgfx
