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

#include "core/GlyphRunList.h"
#include "tgfx/core/Rect.h"

namespace tgfx {
class Atlas;

struct AtlasLocator {
  size_t imageIndex = 0;
  Rect location = tgfx::Rect::MakeEmpty();
  Rect glyphBounds = tgfx::Rect::MakeEmpty();
};

class TextAtlas {
 public:
  static std::unique_ptr<TextAtlas> Make(const Context* context,
                                         std::shared_ptr<GlyphRunList> glyphRunList, float scale,
                                         const Stroke* stroke);

  ~TextAtlas();

  bool getLocator(GlyphID glyphId, AtlasLocator* locator) const;

  std::shared_ptr<Image> getAtlasImage(size_t imageIndex) const;

  size_t memoryUsage() const;

 private:
  TextAtlas(Atlas* atlas) : maskAtlas(atlas) {
  }

  Atlas* maskAtlas = nullptr;
};
}  // namespace tgfx