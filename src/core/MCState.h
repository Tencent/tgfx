/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/core/Matrix.h"
#include "tgfx/core/Path.h"

namespace tgfx {
class MCState {
 public:
  explicit MCState(const Matrix& matrix) : matrix(matrix) {
    clip.toggleInverseFillType();
  }

  explicit MCState(Path initClip) : clip(std::move(initClip)) {
  }

  MCState(const Matrix& matrix, Path clip) : matrix(matrix), clip(std::move(clip)) {
  }

  MCState() {
    clip.toggleInverseFillType();
  }

  Matrix matrix = Matrix::I();
  Path clip = {};
};
}  // namespace tgfx
