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

#include <optional>
#include <unordered_map>
#include "gpu/resources/ResourceKey.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Image.h"

namespace tgfx {
class TextureProxy;

struct SubTreeCacheInfo {
  std::shared_ptr<Image> image = nullptr;
  Matrix matrix = Matrix::I();
};

class SubTreeCache {
 public:
  SubTreeCache() = default;

  const UniqueKey& uniqueKey() const {
    return _uniqueKey;
  }

  void addCache(Context* context, int imageWidth, int imageHeight,
                std::shared_ptr<TextureProxy> textureProxy, const Matrix& imageMatrix);

  std::optional<SubTreeCacheInfo> getSubTreeCacheInfo(Context* context, int imageWidth,
                                                      int imageHeight) const;

 private:
  UniqueKey _uniqueKey = UniqueKey::Make();
  ResourceKeyMap<Matrix> _sizeMatrices = {};

  UniqueKey makeSizeKey(int width, int height) const;
};
}  // namespace tgfx
