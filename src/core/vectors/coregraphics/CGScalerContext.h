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

#include <mutex>
#include <unordered_map>
#include "CGTypeface.h"
#include "core/PixelBuffer.h"
#include "core/ScalerContext.h"
#include "tgfx/core/ImageInfo.h"

namespace tgfx {
class CGScalerContext : public ScalerContext {
 public:
  CGScalerContext(std::shared_ptr<Typeface> typeface, float size);

  ~CGScalerContext() override;

  FontMetrics getFontMetrics() const override;

  Rect getBounds(GlyphID glyphID, bool fauxBold, bool fauxItalic) const override;

  float getAdvance(GlyphID glyphID, bool verticalText) const override;

  Point getVerticalOffset(GlyphID glyphID) const override;

  bool generatePath(GlyphID glyphID, bool fauxBold, bool fauxItalic, Path* path) const override;

  Rect getImageTransform(GlyphID glyphID, bool fauxBold, const Stroke* stroke,
                         Matrix* matrix) const override;

  bool readPixels(GlyphID glyphID, bool fauxBold, const Stroke* stroke, const ImageInfo& dstInfo,
                  void* dstPixels, const Point& glyphOffset) const override;
  float getBackingSize() const override;

 private:
  FontMetrics computeFontMetrics() const;

  float fauxBoldScale = 1.0f;
  CTFontRef ctFont = nullptr;
  CTFontRef backingFont = nullptr;
  FontMetrics fontMetrics = {};

  // Caching for performance optimization
  mutable std::mutex advanceCacheMutex = {};
  mutable std::unordered_map<GlyphID, float> advanceCacheH = {};
  mutable std::unordered_map<GlyphID, float> advanceCacheV = {};

  struct CGBoundsKey {
    GlyphID glyphID;
    bool fauxBold;
    bool fauxItalic;

    bool operator==(const CGBoundsKey& other) const {
      return glyphID == other.glyphID && fauxBold == other.fauxBold && fauxItalic == other.fauxItalic;
    }
  };

  struct CGBoundsKeyHash {
    size_t operator()(const CGBoundsKey& key) const {
      return std::hash<GlyphID>()(key.glyphID) ^ (std::hash<bool>()(key.fauxBold) << 1) ^
             (std::hash<bool>()(key.fauxItalic) << 2);
    }
  };

  mutable std::mutex boundsCacheMutex = {};
  mutable std::unordered_map<CGBoundsKey, Rect, CGBoundsKeyHash> boundsCache = {};
};
}  // namespace tgfx
