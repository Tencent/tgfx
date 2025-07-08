/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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
 * A Rasterizer that rasterizes a set of glyphs.
 */
class TextRasterizer : public Rasterizer {
 public:
  TextRasterizer(int width, int height, std::shared_ptr<GlyphRunList> glyphRunList, bool antiAlias,
                 const Matrix& matrix, const Stroke* stroke);

  ~TextRasterizer() override;

 protected:
  std::shared_ptr<ImageBuffer> onMakeBuffer(bool tryHardware) const override;

 private:
  std::shared_ptr<GlyphRunList> glyphRunList = nullptr;
  bool antiAlias = true;
  Matrix matrix = {};
  Stroke* stroke = nullptr;
};
}  // namespace tgfx
