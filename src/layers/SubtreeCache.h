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

#pragma once

#include <memory>
#include <unordered_map>
#include "Render3DContext.h"
#include "gpu/resources/ResourceKey.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Matrix3D.h"

namespace tgfx {
class TextureProxy;
class ColorSpace;

class SubtreeCache {
 public:
  explicit SubtreeCache(int maxSize) : _maxSize(maxSize) {
  }

  const UniqueKey& uniqueKey() const {
    return _uniqueKey;
  }

  int maxSize() const {
    return _maxSize;
  }

  void addCache(Context* context, int longEdge, std::shared_ptr<TextureProxy> textureProxy,
                const Matrix& imageMatrix, const std::shared_ptr<ColorSpace>& colorSpace);

  bool hasCache(Context* context, int longEdge) const;

  void draw(Context* context, int longEdge, Canvas* canvas, const Paint& paint,
            const Matrix3D* transform3D) const;

 private:
  struct CacheEntry {
    Matrix imageMatrix = {};
    std::shared_ptr<ColorSpace> colorSpace = nullptr;
  };
  int _maxSize = 0;
  UniqueKey _uniqueKey = UniqueKey::Make();
  ResourceKeyMap<CacheEntry> cacheEntries = {};

  UniqueKey makeSizeKey(int longEdge) const;
};
}  // namespace tgfx
