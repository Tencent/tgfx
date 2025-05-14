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

#include "core/GlyphDrawer.h"

namespace tgfx {
class WebGlyphDrawer : public GlyphDrawer {
 public:
  explicit WebGlyphDrawer(float resolutionScale, bool antiAlias, bool needsGammaCorrection)
      : GlyphDrawer(resolutionScale, antiAlias, needsGammaCorrection) {
  }

 protected:
  bool onFillGlyph(const GlyphFace* glyphFace, GlyphID glyphID, const Stroke* stroke,
                   const Rect& glyphBounds, const ImageInfo& dstInfo, void* dstPixels) override;

  bool onFillPath(const Path&, const ImageInfo&, void*) override {
    return false;
  }
};
}  // namespace tgfx