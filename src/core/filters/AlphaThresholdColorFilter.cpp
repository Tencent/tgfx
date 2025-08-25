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

#include "AlphaThresholdColorFilter.h"
#include "core/utils/Types.h"
#include "gpu/processors/AlphaThresholdFragmentProcessor.h"

namespace tgfx {

std::shared_ptr<ColorFilter> ColorFilter::AlphaThreshold(float threshold) {
  threshold = std::max(threshold, 0.0f);
  return std::make_shared<AlphaThresholdColorFilter>(threshold);
}

std::optional<Color> AlphaThresholdColorFilter::tryFilterColor(const Color& input) const {
  if (input.alpha >= threshold) {
    return std::make_optional<Color>(input.red, input.green, input.blue, 1.0f);
  } else {
    // When the generated color’s alpha is 0, premultiplication can zero out all channels, causing
    // data loss and computation errors.
    return std::nullopt;
  }
}

bool AlphaThresholdColorFilter::isEqual(const ColorFilter* colorFilter) const {
  auto type = Types::Get(colorFilter);
  if (type != Types::ColorFilterType::AlphaThreshold) {
    return false;
  }
  auto other = static_cast<const AlphaThresholdColorFilter*>(colorFilter);
  return threshold == other->threshold;
}

PlacementPtr<FragmentProcessor> AlphaThresholdColorFilter::asFragmentProcessor(
    Context* context) const {
  return AlphaThresholdFragmentProcessor::Make(context->drawingBuffer(), threshold);
}

}  // namespace tgfx
