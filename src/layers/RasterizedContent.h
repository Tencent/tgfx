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

#include "tgfx/core/Image.h"
#include "tgfx/core/Matrix.h"

namespace tgfx {
class RasterizedContent {
 public:
  RasterizedContent(uint32_t contextID, std::shared_ptr<Image> image, const Matrix& matrix)
      : _contextID(contextID), _image(std::move(image)), _matrix(matrix) {
  }

  uint32_t contextID() const {
    return _contextID;
  }

  std::shared_ptr<Image> image() const {
    return _image;
  }

  const Matrix& matrix() const {
    return _matrix;
  }

 private:
  uint32_t _contextID = 0;
  std::shared_ptr<Image> _image = nullptr;
  Matrix _matrix = Matrix::I();
};
}  // namespace tgfx
