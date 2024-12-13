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

#include "GlyphShape.h"
#include "core/GlyphRunList.h"
#include "core/utils/Log.h"

namespace tgfx {
std::shared_ptr<Shape> Shape::MakeFrom(std::shared_ptr<TextBlob> textBlob) {
  auto glyphRunLists = GlyphRunList::Unwrap(textBlob.get());
  if (glyphRunLists == nullptr || glyphRunLists->size() != 1) {
    return nullptr;
  }
  auto glyphRunList = (*glyphRunLists)[0];
  if (!glyphRunList->hasOutlines()) {
    return nullptr;
  }
  return std::make_shared<GlyphShape>(std::move(glyphRunList));
}

Rect GlyphShape::getBounds(float resolutionScale) const {
  return glyphRunList->getBounds(resolutionScale);
}

Path GlyphShape::getPath(float resolutionScale) const {
  Path path = {};
  if (!glyphRunList->getPath(&path, resolutionScale)) {
    LOGE("TextShape::getPath() Failed to get path from GlyphRunList!");
    return {};
  }
  return path;
}
}  // namespace tgfx
