/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "LumaColorFilter.h"
#include "gpu/processors/LumaFragmentProcessor.h"

namespace tgfx {

std::shared_ptr<ColorFilter> ColorFilter::Luma() {
  return std::make_shared<LumaColorFilter>();
}

PlacementPtr<FragmentProcessor> LumaColorFilter::asFragmentProcessor(Context* context) const {
  return LumaFragmentProcessor::Make(context->drawingBuffer());
}

std::optional<Color> LumaColorFilter::tryFilterColor(const Color& input) const {
  /** See ITU-R Recommendation BT.709 at http://www.itu.int/rec/R-REC-BT.709/ .*/
  // Must use premultiplied color to compute luma. Othereise, use 'MatrixColorFilter' instead.
  const Color pmColor = input.premultiply();
  float luma = pmColor.red * 0.2126f + pmColor.green * 0.7152f + pmColor.blue * 0.0722f;
  // Return non-premultiplied RGBA color.
  return Color(1.0f, 1.0f, 1.0f, luma);
}

}  // namespace tgfx
