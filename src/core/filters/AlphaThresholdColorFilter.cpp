/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "core/utils/Caster.h"
#include "gpu/processors/AlphaThresholdFragmentProcessor.h"

namespace tgfx {

std::shared_ptr<ColorFilter> ColorFilter::AlphaThreshold(float threshold) {
  threshold = std::max(threshold, 0.0f);
  return std::make_shared<AlphaThresholdColorFilter>(threshold);
}

bool AlphaThresholdColorFilter::isEqual(const ColorFilter* colorFilter) const {
  auto other = Caster::AsAlphaThresholdColorFilter(colorFilter);
  return other && threshold == other->threshold;
}

PlacementPtr<FragmentProcessor> AlphaThresholdColorFilter::asFragmentProcessor(
    Context* context) const {
  return AlphaThresholdFragmentProcessor::Make(context->drawingBuffer(), threshold);
}

}  // namespace tgfx
