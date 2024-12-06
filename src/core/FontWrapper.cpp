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

#include "tgfx/core/FontWrapper.h"
#include "utils/MathExtra.h"

namespace tgfx {
bool FontWrapper::hasColor() const {
  return _font.hasColor();
}

bool FontWrapper::hasOutlines() const {
  return _font.hasOutlines();
}

std::shared_ptr<RenderFont> FontWrapper::makeScaled(float scale) {
  if (FloatNearlyZero(scale)) {
    return nullptr;
  }
  auto size = _font.getSize() * scale;
  return std::make_shared<FontWrapper>(_font.makeWithSize(size));
}

bool FontWrapper::getPath(GlyphID glyphID, Path* path) const {
  return _font.getPath(glyphID, path);
}

std::shared_ptr<Image> FontWrapper::getImage(GlyphID glyphID, Matrix* matrix) const {
  return _font.getImage(glyphID, matrix);
}

Rect FontWrapper::getBounds(GlyphID glyphID) const {
  return _font.getBounds(glyphID);
}
}  // namespace tgfx