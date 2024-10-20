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

#pragma once

#include "tgfx/layers/LayerContent.h"

namespace tgfx {
class RasterizedContent : public LayerContent {
 public:
  RasterizedContent(uint32_t deviceID, std::shared_ptr<Image> image, const Matrix& matrix)
      : _deviceID(deviceID), image(std::move(image)), matrix(matrix) {
  }

  /**
   * Returns the unique ID of the associated GPU device.
   */
  uint32_t deviceID() const {
    return _deviceID;
  }

  Rect getBounds() const override;

  void draw(Canvas* canvas, const Paint& paint) const override;

 private:
  uint32_t _deviceID = 0;
  std::shared_ptr<Image> image = nullptr;
  Matrix matrix = Matrix::I();
};
}  // namespace tgfx
