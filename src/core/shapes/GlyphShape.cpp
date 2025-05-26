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

#include "GlyphShape.h"

namespace tgfx {
std::shared_ptr<Shape> Shape::MakeFrom(std::shared_ptr<GlyphFace> glyphFace, GlyphID glyphID) {
  if (glyphFace == nullptr || glyphID == 0) {
    return nullptr;
  }
  if (!glyphFace->hasOutlines()) {
    return nullptr;
  }
  return std::make_shared<GlyphShape>(std::move(glyphFace), glyphID);
}

GlyphShape::GlyphShape(std::shared_ptr<GlyphFace> glyphFace, GlyphID glyphID)
    : glyphFace(std::move(glyphFace)), glyphID(glyphID) {
}

Path GlyphShape::getPath() const {
  Path path = {};
  if (!glyphFace->getPath(glyphID, &path)) {
    return {};
  }
  return path;
}

Rect GlyphShape::getBounds() const {
  return glyphFace->getBounds(glyphID);
}
}  // namespace tgfx