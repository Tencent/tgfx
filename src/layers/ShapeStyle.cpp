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

#include "tgfx/layers/ShapeStyle.h"

namespace tgfx {

std::shared_ptr<ShapeStyle> ShapeStyle::Make(const Color& color, BlendMode blendMode) {
  return std::shared_ptr<ShapeStyle>(new ShapeStyle(color, nullptr, blendMode));
}

std::shared_ptr<ShapeStyle> ShapeStyle::Make(std::shared_ptr<Shader> shader, float alpha,
                                             BlendMode blendMode) {
  if (shader == nullptr) {
    return nullptr;
  }
  Color color = Color::White();
  color.alpha = alpha;
  return std::shared_ptr<ShapeStyle>(new ShapeStyle(color, std::move(shader), blendMode));
}

}  // namespace tgfx
