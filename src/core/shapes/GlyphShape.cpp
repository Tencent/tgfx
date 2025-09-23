/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "GlyphShape.h"
#include "core/utils/MathExtra.h"

namespace tgfx {
std::shared_ptr<Shape> Shape::MakeFrom(Font font, GlyphID glyphID) {
  if (glyphID == 0) {
    return nullptr;
  }
  if (!font.hasOutlines()) {
    return nullptr;
  }
  return std::make_shared<GlyphShape>(std::move(font), glyphID);
}

GlyphShape::GlyphShape(Font font, GlyphID glyphID) : font(std::move(font)), glyphID(glyphID) {
}

Path GlyphShape::onGetPath(float resolutionScale) const {
  if (FloatNearlyZero(resolutionScale)) {
    return {};
  }
  Path path = {};
  if (FloatNearlyEqual(resolutionScale, 1.0f)) {
    if (!font.getPath(glyphID, &path)) {
      return {};
    }
  } else {
    auto scaledFont = font.makeWithSize(font.getSize() * resolutionScale);
    if (!scaledFont.getPath(glyphID, &path)) {
      return {};
    }
    auto inverseMatrix = Matrix::MakeScale(1.f / resolutionScale, 1.f / resolutionScale);
    path.transform(inverseMatrix);
  }
  return path;
}

Rect GlyphShape::getBounds() const {
  return font.getBounds(glyphID);
}
}  // namespace tgfx