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

#include "Context3DCompositor.h"
#include "tgfx/core/Matrix3D.h"

namespace tgfx {

class Render3DContext {
 public:
  explicit Render3DContext(std::shared_ptr<Context3DCompositor> compositor, const Rect& renderRect,
                           const Matrix3D& depthMatrix)
      : _compositor(std::move(compositor)), _renderRect(renderRect), _depthMatrix(depthMatrix) {
  }

  std::shared_ptr<Context3DCompositor> compositor() {
    return _compositor;
  }

  const Rect& renderRect() const {
    return _renderRect;
  }

  const Matrix3D& depthMatrix() const {
    return _depthMatrix;
  }

 private:
  std::shared_ptr<Context3DCompositor> _compositor;

  Rect _renderRect;

  // The depth mapping matrix applied to all layers within the 3D render context, which maps the
  // depth of all layers to the range [-1, 1].
  Matrix3D _depthMatrix;
};

}  // namespace tgfx