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

#pragma once

#include <emscripten/val.h>
#include "core/ScalerContext.h"

namespace tgfx {
class WebScalerContext : public ScalerContext {
 public:
  WebScalerContext(std::shared_ptr<Typeface> typeface, float size, emscripten::val scalerContext);

  bool asyncSupport() const override {
    return false;
  }

  Rect getBounds(GlyphID glyphID, bool fauxBold, bool fauxItalic) const override;

  float getAdvance(GlyphID glyphID, bool verticalText) const override;

  Point getVerticalOffset(GlyphID glyphID) const override;

  bool generatePath(GlyphID glyphID, bool fauxBold, bool fauxItalic, Path* path) const override;

  Rect getImageTransform(GlyphID glyphID, bool fauxBold, const Stroke* stroke,
                         Matrix* matrix) const override;

  bool readPixels(GlyphID glyphID, bool fauxBold, const Stroke* stroke, const ImageInfo& dstInfo,
                  void* dstPixels, const Point& glyphOffset) const override;

  /**
   * Returns a Canvas containing the rendered glyph. The returned Canvas can be directly uploaded to
   * a texture without reading pixels. Returns null if the glyph cannot be rendered.
   */
  emscripten::val getGlyphCanvas(GlyphID glyphID, bool fauxBold, const Stroke* stroke,
                                 int padding) const;

 protected:
  FontMetrics onComputeFontMetrics() const override;

 private:
  emscripten::val scalerContext = emscripten::val::null();

  std::string getText(GlyphID glyphID) const;
};
}  // namespace tgfx
