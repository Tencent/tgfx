/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "ModeColorFilter.h"
#include "core/utils/Types.h"
#include "gpu/processors/ConstColorProcessor.h"
#include "gpu/processors/XfermodeFragmentProcessor.h"

namespace tgfx {
static bool IsNoOps(float alpha, BlendMode mode) {
  return BlendMode::Dst == mode ||
         (0.f == alpha &&
          (BlendMode::DstOver == mode || BlendMode::DstOut == mode || BlendMode::SrcATop == mode ||
           BlendMode::Xor == mode || BlendMode::Darken == mode)) ||
         (1.f == alpha && BlendMode::DstIn == mode);
}

std::shared_ptr<ColorFilter> ColorFilter::Blend(Color color, BlendMode mode) {
  float alpha = color.alpha;
  if (BlendMode::Clear == mode) {
    color = Color::Transparent();
    mode = BlendMode::Src;
  } else if (BlendMode::SrcOver == mode) {
    if (0.f == alpha) {
      mode = BlendMode::Dst;
    } else if (1.f == alpha) {
      mode = BlendMode::Src;
    }
  }
  // weed out combinations that are no-ops, and just return null.
  if (IsNoOps(alpha, mode)) {
    return nullptr;
  }
  return std::make_shared<ModeColorFilter>(color, mode);
}

bool ModeColorFilter::asColorMode(Color* color, BlendMode* mode) const {
  if (color) {
    *color = this->color;
  }
  if (mode) {
    *mode = this->mode;
  }
  return true;
}

bool ModeColorFilter::isEqual(const ColorFilter* colorFilter) const {
  auto type = Types::Get(colorFilter);
  if (type != Types::ColorFilterType::Blend) {
    return false;
  }
  auto other = static_cast<const ModeColorFilter*>(colorFilter);
  return color == other->color && mode == other->mode;
}

PlacementPtr<FragmentProcessor> ModeColorFilter::asFragmentProcessor(
    Context* context, std::shared_ptr<ColorSpace> dstColorSpace) const {
  auto dstColor = color.makeColorSpace(std::move(dstColorSpace));
  auto processor = ConstColorProcessor::Make(context->drawingAllocator(), dstColor.premultiply(),
                                             InputMode::Ignore);
  return XfermodeFragmentProcessor::MakeFromSrcProcessor(context->drawingAllocator(),
                                                         std::move(processor), mode);
}

bool ModeColorFilter::isAlphaUnchanged() const {
  return BlendMode::Dst == mode || BlendMode::SrcATop == mode;
}
}  // namespace tgfx
