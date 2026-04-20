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

#include "ImageAdjustColorFilter.h"
#include "core/utils/Types.h"
#include "gpu/processors/ImageAdjustFragmentProcessor.h"

namespace tgfx {

std::shared_ptr<ColorFilter> ColorFilter::ImageAdjust(float exposure, float contrast,
                                                      float saturation, float temperature,
                                                      float tint, float highlights, float shadows) {
  return std::make_shared<ImageAdjustColorFilter>(exposure, contrast, saturation, temperature, tint,
                                                  highlights, shadows);
}

bool ImageAdjustColorFilter::isEqual(const ColorFilter* colorFilter) const {
  auto type = Types::Get(colorFilter);
  if (type != Types::ColorFilterType::ImageAdjust) {
    return false;
  }
  auto other = static_cast<const ImageAdjustColorFilter*>(colorFilter);
  return exposure == other->exposure && contrast == other->contrast &&
         saturation == other->saturation && temperature == other->temperature &&
         tint == other->tint && highlights == other->highlights && shadows == other->shadows;
}

PlacementPtr<FragmentProcessor> ImageAdjustColorFilter::asFragmentProcessor(
    Context* context, const std::shared_ptr<ColorSpace>&) const {
  return ImageAdjustFragmentProcessor::Make(context->drawingAllocator(), exposure, contrast,
                                            saturation, temperature, tint, highlights, shadows);
}

}  // namespace tgfx
