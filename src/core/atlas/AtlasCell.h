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

#include "core/atlas/AtlasTypes.h"
#include "tgfx/core/BytesKey.h"
#include "tgfx/core/Matrix.h"

namespace tgfx {
class AtlasCell {
 public:
  uint16_t id() const {
    return _id;
  }

  const BytesKey& key() const {
    return _key;
  }

  const AtlasLocator& locator() const {
    return _locator;
  }

  void setAtlasLocator(const AtlasLocator& locator) {
    _locator = locator;
  }

  MaskFormat maskFormat() const {
    return _maskFormat;
  }

  uint16_t width() const {
    return _width;
  }

  uint16_t height() const {
    return _height;
  }

  const Matrix& matrix() const {
    return _matrix;
  }

 private:
  BytesKey _key;
  Matrix _matrix;
  AtlasLocator _locator;
  MaskFormat _maskFormat = MaskFormat::A8;
  uint16_t _width = 0;
  uint16_t _height = 0;
  uint16_t _id = 0;

  friend class AtlasSource;
  friend class TextRender;
};

struct AtlasCellLocator {
  Matrix matrix = {};  // The cell's transformation matrix
  AtlasLocator atlasLocator;
};
}  //namespace tgfx
