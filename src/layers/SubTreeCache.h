/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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
#include "gpu/resources/ResourceKey.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Matrix3D.h"

namespace tgfx {
class TextureProxy;

class SubTreeCacheDrawer {
 public:
  SubTreeCacheDrawer(std::shared_ptr<Image> image, const Matrix& matrix)
      : _image(std::move(image)), _matrix(matrix) {
  }

  void draw(Canvas* canvas, const Paint& paint, const Matrix3D* transform3D) const;

 private:
  std::shared_ptr<Image> _image = nullptr;
  Matrix _matrix = Matrix::I();
};

class SubTreeCache {
 public:
  SubTreeCache() = default;

  const UniqueKey& uniqueKey() const {
    return _uniqueKey;
  }

  void addCache(Context* context, std::shared_ptr<TextureProxy> textureProxy,
                const Matrix& imageMatrix);

  std::unique_ptr<SubTreeCacheDrawer> getDrawer(Context* context, int longEdge) const;

 private:
  UniqueKey _uniqueKey = UniqueKey::Make();
  ResourceKeyMap<Matrix> _sizeMatrices = {};

  UniqueKey makeSizeKey(int longEdge) const;
};
}  // namespace tgfx
