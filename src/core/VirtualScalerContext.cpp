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

#include "VirtualScalerContext.h"
#include "tgfx/core/VirtualTypeface.h"

namespace tgfx {
VirtualScalerContext::VirtualScalerContext(std::shared_ptr<Typeface> typeface, float size)
    : ScalerContext(std::move(typeface), size) {
}

FontMetrics VirtualScalerContext::getFontMetrics() const {
  return FontMetrics();
}

Rect VirtualScalerContext::getBounds(GlyphID glyphID, bool fauxBold,
                                     bool fauxItalic) const {
  return ConvertTypeface(typeface)->GetBounds(typeface, glyphID, fauxBold, fauxItalic);
}

float VirtualScalerContext::getAdvance(GlyphID /*glyphID*/, bool /*verticalText*/) const {
  return 0;
}

Point VirtualScalerContext::getVerticalOffset(GlyphID /*glyphID*/) const {
  return Point::Make(0, 0);
}

bool VirtualScalerContext::generatePath(GlyphID glyphID, bool fauxBold, bool fauxItalic,
                                        Path* path) const {
  return ConvertTypeface(typeface)->GetPath(typeface, glyphID, fauxBold, fauxItalic, path);
}

Size VirtualScalerContext::getImageTransform(GlyphID glyphID, Matrix* matrix) const {
  return ConvertTypeface(typeface)->GetImageTransform(typeface, glyphID, matrix);
}

std::shared_ptr<ImageBuffer> VirtualScalerContext::generateImage(GlyphID glyphID, bool tryHardware) const {
  return ConvertTypeface(typeface)->GetImage(typeface, glyphID, tryHardware);
}

std::shared_ptr<VirtualTypeface> VirtualScalerContext::ConvertTypeface(
    const std::shared_ptr<Typeface>& typeface) {
  return std::dynamic_pointer_cast<VirtualTypeface>(typeface);
}
}  // namespace tgfx
