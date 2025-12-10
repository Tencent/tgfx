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

#include <unordered_map>
#include "gpu/resources/ResourceKey.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Image.h"

namespace tgfx {
class RasterizedCache {
 public:
  /**
   * Creates a RasterizedCache by rasterizing the image to a texture and caching it with an
   * internally generated uniqueKey. The texture can be retrieved later using the uniqueKey.
   * @param context The GPU context to use for creating the texture.
   */
  static std::unique_ptr<RasterizedCache> MakeFrom(Context* context);

  explicit RasterizedCache(uint32_t contextID)
      : _contextID(contextID), _uniqueKey(UniqueKey::Make()) {
  }

  uint32_t contextID() const {
    return _contextID;
  }

  const UniqueKey& uniqueKey() const {
    return _uniqueKey;
  }

  bool valid(Context* context, float scale) const;

  std::shared_ptr<Image> addScaleCache(Context* context, float contentScale,
                                       std::shared_ptr<Image> image, const Matrix& imageMatrix);

  void draw(Context* context, Canvas* canvas, bool antiAlias, float alpha,
            const std::shared_ptr<MaskFilter>& mask, BlendMode blendMode = BlendMode::SrcOver,
            const Matrix3D* transform = nullptr) const;

 private:
  uint32_t _contextID = 0;
  UniqueKey _uniqueKey = {};
  mutable std::unordered_map<int64_t, Matrix> _scaleMatrices = {};

  UniqueKey MakeScaleKey(float scale) const;
};
}  // namespace tgfx
