/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "tgfx/core/Orientation.h"

namespace tgfx {
Matrix OrientationToMatrix(Orientation orientation, int width, int height) {
  auto w = static_cast<float>(width);
  auto h = static_cast<float>(height);
  switch (orientation) {
    case Orientation::TopLeft:
      return Matrix::I();
    case Orientation::TopRight:
      return Matrix::MakeAll(-1, 0, w, 0, 1, 0);
    case Orientation::BottomRight:
      return Matrix::MakeAll(-1, 0, w, 0, -1, h);
    case Orientation::BottomLeft:
      return Matrix::MakeAll(1, 0, 0, 0, -1, h);
    case Orientation::LeftTop:
      return Matrix::MakeAll(0, 1, 0, 1, 0, 0);
    case Orientation::RightTop:
      return Matrix::MakeAll(0, -1, h, 1, 0, 0);
    case Orientation::RightBottom:
      return Matrix::MakeAll(0, -1, h, -1, 0, w);
    case Orientation::LeftBottom:
      return Matrix::MakeAll(0, 1, 0, -1, 0, w);
  }
  return Matrix::I();
}

bool OrientationSwapsWidthHeight(Orientation origin) {
  return origin >= Orientation::LeftTop;
}
}  // namespace tgfx
