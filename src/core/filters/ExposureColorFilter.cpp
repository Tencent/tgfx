/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "ExposureColorFilter.h"
#include "core/utils/Types.h"
#include "gpu/processors/ExposureFragmentProcessor.h"

namespace tgfx {

std::shared_ptr<ColorFilter> ColorFilter::Exposure(float exposure) {
  return std::make_shared<ExposureColorFilter>(exposure);
}

bool ExposureColorFilter::isEqual(const ColorFilter* colorFilter) const {
  auto type = Types::Get(colorFilter);
  if (type != Types::ColorFilterType::Exposure) {
    return false;
  }
  auto other = static_cast<const ExposureColorFilter*>(colorFilter);
  return exposure == other->exposure;
}

PlacementPtr<FragmentProcessor> ExposureColorFilter::asFragmentProcessor(
    Context* context, const std::shared_ptr<ColorSpace>&) const {
  return ExposureFragmentProcessor::Make(context->drawingAllocator(), exposure);
}

}  // namespace tgfx
