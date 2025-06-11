/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/core/CustomTypefaceBuilder.h"
#include "core/utils/UniqueID.h"

namespace tgfx {
void CustomTypefaceBuilder::setFontName(const std::string& fontFamily,
                                        const std::string& fontStyle) {
  _fontFamily = fontFamily;
  _fontStyle = fontStyle;
}

void CustomTypefaceBuilder::setMetrics(const FontMetrics& metrics) {
  _fontMetrics = metrics;
}

void CustomTypefaceBuilder::updateMetricsBounds(const Rect& bounds, bool firstTime) {
  if (firstTime) {
    _fontMetrics.top = bounds.top;
    _fontMetrics.bottom = bounds.bottom;
    _fontMetrics.xMin = bounds.left;
    _fontMetrics.xMax = bounds.right;
  } else {
    _fontMetrics.top = std::min(_fontMetrics.top, bounds.top);
    _fontMetrics.bottom = std::max(_fontMetrics.bottom, bounds.bottom);
    _fontMetrics.xMin = std::min(_fontMetrics.xMin, bounds.left);
    _fontMetrics.xMax = std::max(_fontMetrics.xMax, bounds.right);
  }
}
}  // namespace tgfx
