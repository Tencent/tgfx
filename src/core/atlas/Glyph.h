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

#include "../FontGlyphFace.h"
#include "core/atlas/AtlasTypes.h"
#include "tgfx/core/BytesKey.h"

namespace tgfx {
class Glyph {
 public:
  ~Glyph();

  GlyphID glyphId() const {
    return _glyphId;
  }

  const BytesKey& key() const {
    return _key;
  }

  const AtlasLocator& locator() const {
    return _locator;
  }

  void setImage(void* image) {
    _image = image;
  }

  void setAtlasLocator(const AtlasLocator& locator) {
    _locator = locator;
  }

  void* image() const {
    return _image;
  }

  MaskFormat maskFormat() const {
    return _maskFormat;
  }

  Point position() const {
    return _position;
  }

  int width() const {
    return _width;
  }

  int height() const {
    return _height;
  }

 private:
  Point _position = Point::Zero();
  int _width = 0;
  int _height = 0;
  GlyphID _glyphId;
  MaskFormat _maskFormat;
  void* _image = nullptr;
  BytesKey _key;
  AtlasLocator _locator;

  friend class AtlasSource;
  friend class RenderContext;
};

struct DrawGlyph {
  MaskFormat maskFormat;
  GlyphID glyphId;
  std::shared_ptr<GlyphFace> glyphFace;
  Point position = Point::Zero();
  AtlasLocator locator;
};
}  //namespace tgfx