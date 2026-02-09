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

#include "ScalerContext.h"

namespace tgfx {
class EmptyScalerContext : public ScalerContext {
 public:
  explicit EmptyScalerContext(float size) : ScalerContext(nullptr, size) {
  }

  Rect getBounds(GlyphID, bool, bool) const override {
    return {};
  }

  float getAdvance(GlyphID, bool) const override {
    return 0.0f;
  }

  Point getVerticalOffset(GlyphID) const override {
    return {};
  }

  bool generatePath(GlyphID, bool, bool, Path*) const override {
    return false;
  }

  Rect getImageTransform(GlyphID, bool, const Stroke*, Matrix*) const override {
    return {};
  }

  bool readPixels(GlyphID, bool, const Stroke*, const ImageInfo&, void*,
                  const Point&) const override {
    return false;
  }

 protected:
  FontMetrics onComputeFontMetrics() const override {
    return {};
  }
};

std::shared_ptr<ScalerContext> ScalerContext::MakeEmpty(float size) {
  if (size < 0) {
    size = 0;
  }
  if (size == 0) {
    static auto EmptyContext = std::make_shared<EmptyScalerContext>(0.0f);
    return EmptyContext;
  }
  return std::make_shared<EmptyScalerContext>(size);
}

ScalerContext::ScalerContext(std::shared_ptr<Typeface> typeface, float size)
    : typeface(std::move(typeface)), textSize(size) {
}

FontMetrics ScalerContext::getFontMetrics() const {
  std::call_once(fontMetricsOnceFlag, [this] { fontMetricsCache = onComputeFontMetrics(); });
  return fontMetricsCache;
}
}  // namespace tgfx
