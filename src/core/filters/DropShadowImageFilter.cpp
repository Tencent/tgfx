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

#include "DropShadowImageFilter.h"
#include "core/images/TextureImage.h"
#include "core/utils/NeedMipmaps.h"
#include "gpu/OpContext.h"
#include "gpu/TPArgs.h"
#include "gpu/processors/ConstColorProcessor.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/XfermodeFragmentProcessor.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/core/ColorFilter.h"

namespace tgfx {
std::shared_ptr<ImageFilter> ImageFilter::DropShadow(float dx, float dy, float blurrinessX,
                                                     float blurrinessY, const Color& color) {
  return std::make_shared<DropShadowImageFilter>(dx, dy, blurrinessX, blurrinessY, color, false);
}

std::shared_ptr<ImageFilter> ImageFilter::DropShadowOnly(float dx, float dy, float blurrinessX,
                                                         float blurrinessY, const Color& color) {
  return std::make_shared<DropShadowImageFilter>(dx, dy, blurrinessX, blurrinessY, color, true);
}

DropShadowImageFilter::DropShadowImageFilter(float dx, float dy, float blurrinessX,
                                             float blurrinessY, const Color& color, bool shadowOnly)
    : dx(dx), dy(dy), blurFilter(ImageFilter::Blur(blurrinessX, blurrinessY)), color(color),
      shadowOnly(shadowOnly) {
}

Rect DropShadowImageFilter::onFilterBounds(const Rect& srcRect) const {
  auto bounds = srcRect;
  bounds.offset(dx, dy);
  if (blurFilter != nullptr) {
    bounds = blurFilter->filterBounds(bounds);
  }
  if (!shadowOnly) {
    bounds.join(srcRect);
  }
  return bounds;
}

std::unique_ptr<FragmentProcessor> DropShadowImageFilter::asFragmentProcessor(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& sampling,
    const Matrix* uvMatrix) const {
  if (!source->isFlat()) {
    auto needMipmaps = NeedMipmaps(sampling, args.viewMatrix, uvMatrix);
    source = source->makeFlattened(needMipmaps, sampling);
  }
  std::unique_ptr<FragmentProcessor> shadowProcessor;
  auto shadowMatrix = Matrix::MakeTrans(-dx, -dy);
  if (uvMatrix != nullptr) {
    shadowMatrix.preConcat(*uvMatrix);
  }
  if (blurFilter != nullptr) {
    shadowProcessor = blurFilter->asFragmentProcessor(source, args, sampling, &shadowMatrix);
  } else {
    shadowProcessor = FragmentProcessor::Make(source, args, TileMode::Decal, TileMode::Decal,
                                              sampling, &shadowMatrix);
  }
  if (shadowProcessor == nullptr) {
    return nullptr;
  }
  auto colorProcessor = ConstColorProcessor::Make(color, InputMode::Ignore);
  auto colorShadowProcessor = XfermodeFragmentProcessor::MakeFromTwoProcessors(
      std::move(colorProcessor), std::move(shadowProcessor), BlendMode::SrcIn);
  if (shadowOnly) {
    return colorShadowProcessor;
  }
  auto imageProcessor = FragmentProcessor::Make(std::move(source), args, TileMode::Decal,
                                                TileMode::Decal, sampling, uvMatrix);
  return XfermodeFragmentProcessor::MakeFromTwoProcessors(
      std::move(imageProcessor), std::move(colorShadowProcessor), BlendMode::SrcOver);
}

}  // namespace tgfx
