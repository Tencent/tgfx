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

#pragma once

#include "core/Rasterizer.h"

namespace tgfx {
/**
 * An ImageCodec that rasterizes a glyph.
 */
class GlyphImageCodec : public ImageCodec {
 public:
  static std::shared_ptr<GlyphImageCodec> MakeFrom(std::shared_ptr<GlyphFace> glyphFace,
                                                   GlyphID glyphID, float resolutionScale,
                                                   const Stroke* stroke = nullptr);

  ~GlyphImageCodec() override;

  bool isAlphaOnly() const override {
    return !glyphFace->hasColor();
  }

  const Matrix& getImageTransform() const {
    return matrix;
  }

  bool readPixels(const ImageInfo& dstInfo, void* dstPixels) const override;

 private:
  GlyphImageCodec(std::shared_ptr<GlyphFace> glyphFace, GlyphID glyphID, const Rect& bounds,
                  float resolutionScale, const Stroke* stroke);

  std::shared_ptr<GlyphFace> glyphFace = nullptr;
  std::shared_ptr<ImageCodec> imageCodec = nullptr;
  Matrix matrix = {};
  Rect bounds = {};
  Stroke* stroke = nullptr;
  float resolutionScale = 1.f;
  GlyphID glyphID = 0;
};
}  // namespace tgfx
