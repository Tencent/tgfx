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

#include "EffectShape.h"

namespace tgfx {
std::shared_ptr<Shape> Shape::ApplyEffect(std::shared_ptr<Shape> shape,
                                          std::shared_ptr<PathEffect> effect) {

  if (shape == nullptr) {
    return nullptr;
  }
  if (effect == nullptr) {
    return shape;
  }
  return std::make_shared<EffectShape>(std::move(shape), std::move(effect));
}

Rect EffectShape::onGetBounds() const {
  auto bounds = shape->onGetBounds();
  return effect->filterBounds(bounds);
}

Path EffectShape::onGetPath(float resolutionScale) const {
  auto path = shape->onGetPath(resolutionScale);
  effect->filterPath(&path);
  return path;
}

}  // namespace tgfx
