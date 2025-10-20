/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "ColorImageFilter.h"
#include "gpu/processors/ComposeFragmentProcessor.h"
#include "gpu/processors/FragmentProcessor.h"
#include "tgfx/core/ColorFilter.h"

namespace tgfx {

std::shared_ptr<ImageFilter> ImageFilter::ColorFilter(
    std::shared_ptr<class ColorFilter> colorFilter) {
  return std::make_shared<ColorImageFilter>(std::move(colorFilter));
}

ColorImageFilter::ColorImageFilter(std::shared_ptr<tgfx::ColorFilter> filter)
    : filter(std::move(filter)) {
}

PlacementPtr<FragmentProcessor> ColorImageFilter::asFragmentProcessor(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& sampling,
    SrcRectConstraint constraint, const Matrix* uvMatrix,
    std::shared_ptr<ColorSpace> dstColorSpace) const {
  auto imageProcessor =
      FragmentProcessor::Make(source, args, sampling, constraint, uvMatrix, dstColorSpace);
  if (imageProcessor == nullptr) {
    return nullptr;
  }
  auto drawingBuffer = args.context->drawingBuffer();
  auto processor =
      ComposeFragmentProcessor::Make(drawingBuffer, std::move(imageProcessor),
                                     filter->asFragmentProcessor(args.context, dstColorSpace));
  return FragmentProcessor::MulChildByInputAlpha(drawingBuffer, std::move(processor));
}
}  // namespace tgfx
