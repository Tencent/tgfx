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

namespace tgfx {

class Render3DContext {
 public:
  explicit Render3DContext(std::shared_ptr<Context3DCompositor> compositor, const Rect& renderRect)
      : _compositor(std::move(compositor)), _renderRect(renderRect) {
  }

  std::shared_ptr<Context3DCompositor> compositor() {
    return _compositor;
  }

  const Rect& renderRect() const {
    return _renderRect;
  }

 private:
  std::shared_ptr<Context3DCompositor> _compositor = nullptr;
  Rect _renderRect = {};
};

}  // namespace tgfx
