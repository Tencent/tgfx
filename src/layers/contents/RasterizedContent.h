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

#include "tgfx/core/Canvas.h"

namespace tgfx {
class RasterizedContent {
 public:
  RasterizedContent(uint32_t contextID, float contentScale, std::shared_ptr<Image> image,
                    const Matrix& matrix)
      : _contextID(contextID), _contentScale(contentScale), image(std::move(image)),
        matrix(matrix) {
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

  std::shared_ptr<Image> getImage() const {
    return image;
  }

  Matrix getMatrix() const {
    return matrix;
  }

  void draw(Canvas* canvas, bool antiAlias, float alpha, const std::shared_ptr<MaskFilter>& mask,
            BlendMode blendMode = BlendMode::SrcOver) const;

 private:
  uint32_t _contextID = 0;
  float _contentScale = 0.0f;
  std::shared_ptr<Image> image = nullptr;
  Matrix matrix = {};
};
}  // namespace tgfx
