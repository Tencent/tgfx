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

#pragma once

#include <array>
#include "core/GlyphRunList.h"
#include "tgfx/core/GlyphFace.h"
#include "tgfx/core/Stroke.h"

namespace tgfx {
class GlyphDrawer {
 public:
  static std::shared_ptr<GlyphDrawer> Make(float resolutionScale, bool antiAlias,
                                           bool needsGammaCorrection);

  static Rect GetGlyphBounds(const GlyphFace* glyphFace, GlyphID glyphID, float resolutionScale,
                             const Stroke* stroke);

  virtual ~GlyphDrawer() = default;

  bool fillGlyph(const GlyphFace* glyphFace, GlyphID, const Stroke* stroke,
                 const ImageInfo& dstInfo, void* dstPixels);

  bool fillPath(const Path& path, const ImageInfo& dstInfo, void* dstPixels);

 protected:
  static const std::array<uint8_t, 256>& GammaTable();

  explicit GlyphDrawer(float resolutionScale, bool antiAlias, bool needsGammaCorrection);

  virtual bool onFillGlyph(const GlyphFace* glyphFace, GlyphID glyphID, const Stroke* stroke,
                           const Rect& glyphBounds, const ImageInfo& dstInfo, void* dstPixels);

  virtual bool onFillPath(const Path& path, const ImageInfo& dstInfo, void* dstPixels) = 0;

  float resolutionScale = 1.f;
  bool antiAlias = false;
  bool needsGammaCorrection = true;
};
}  // namespace tgfx
