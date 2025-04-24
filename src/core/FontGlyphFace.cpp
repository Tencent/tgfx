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

#include "core/FontGlyphFace.h"

namespace tgfx {
std::shared_ptr<GlyphFace> GlyphFace::Wrap(Font font) {
  if (font.getTypeface() == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<FontGlyphFace>(new FontGlyphFace(std::move(font)));
}

bool FontGlyphFace::hasColor() const {
  return _font.hasColor();
}

bool FontGlyphFace::hasOutlines() const {
  return _font.hasOutlines();
}

std::shared_ptr<GlyphFace> FontGlyphFace::makeScaled(float scale) const {
  if (scale <= 0) {
    return nullptr;
  }
  auto size = _font.getSize() * scale;
  return GlyphFace::Wrap(_font.makeWithSize(size));
}

bool FontGlyphFace::getPath(GlyphID glyphID, Path* path) const {
  return _font.getPath(glyphID, path);
}

std::shared_ptr<ImageCodec> FontGlyphFace::getImage(GlyphID glyphID, Matrix* matrix) const {
  return _font.getImage(glyphID, matrix);
}

Rect FontGlyphFace::getBounds(GlyphID glyphID) const {
  return _font.getBounds(glyphID);
}

bool FontGlyphFace::asFont(Font* font) const {
  if (font != nullptr) {
    *font = _font;
  }

  return true;
}

float FontGlyphFace::getScale() const {
  return _font.getSize();
}

}  // namespace tgfx