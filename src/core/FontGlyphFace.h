/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "tgfx/core/Font.h"
#include "tgfx/core/GlyphFace.h"

namespace tgfx {
class FontGlyphFace final : public GlyphFace {
 public:
  bool hasColor() const override;

  bool hasOutlines() const override;

  std::shared_ptr<GlyphFace> makeScaled(float scale) const override;

  bool getPath(GlyphID glyphID, Path* path) const override;

  std::shared_ptr<Image> getImage(GlyphID glyphID, Matrix* matrix) const override;

  Rect getBounds(GlyphID glyphID) const override;

  bool asFont(Font* font) const override;

 private:
  explicit FontGlyphFace(Font font) : _font(std::move(font)) {
  }

  Font _font = {};

  friend class GlyphFace;
};
}  // namespace tgfx
