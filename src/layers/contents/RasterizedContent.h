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

#include "gpu/resources/ResourceKey.h"
#include "tgfx/core/Canvas.h"

namespace tgfx {
class RasterizedContent {
 public:
  /**
   * Creates a RasterizedContent by rasterizing the image to a texture and caching it with the
   * uniqueKey. The texture can be retrieved later using the uniqueKey.
   */
  static std::unique_ptr<RasterizedContent> MakeFrom(Context* context, float contentScale,
                                                     const std::shared_ptr<Image>& image,
                                                     const Matrix& imageMatrix,
                                                     const UniqueKey& uniqueKey,
                                                     uint32_t renderFlags = 0);

  RasterizedContent(uint32_t contextID, float contentScale, const UniqueKey& uniqueKey,
                    const Matrix& matrix, std::shared_ptr<ColorSpace> colorSpace)
      : _contextID(contextID), _contentScale(contentScale), _uniqueKey(uniqueKey), matrix(matrix),
        _colorSpace(std::move(colorSpace)) {
  }

  /**
   * Returns the unique ID of the associated GPU device.
   */
  uint32_t contextID() const {
    return _contextID;
  }

  float contentScale() const {
    return _contentScale;
  }

  Matrix getMatrix() const {
    return matrix;
  }

  /**
   * Returns true if the cached texture is still valid in the given context.
   */
  bool valid(Context* context) const;

  void draw(Canvas* canvas, bool antiAlias, float alpha, const std::shared_ptr<MaskFilter>& mask,
            BlendMode blendMode = BlendMode::SrcOver, const Matrix3D* transform = nullptr) const;

 private:
  uint32_t _contextID = 0;
  float _contentScale = 0.0f;
  UniqueKey _uniqueKey = {};
  Matrix matrix = {};
  std::shared_ptr<ColorSpace> _colorSpace = nullptr;
};
}  // namespace tgfx
