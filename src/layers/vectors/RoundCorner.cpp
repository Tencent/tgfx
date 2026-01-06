/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "tgfx/layers/vectors/RoundCorner.h"
#include "VectorContext.h"
#include "tgfx/core/PathEffect.h"

namespace tgfx {

void RoundCorner::setRadius(float value) {
  if (_radius == value) {
    return;
  }
  _radius = value;
  _cachedEffect = nullptr;
  invalidateContent();
}

void RoundCorner::apply(VectorContext* context) {
  if (_cachedEffect == nullptr) {
    _cachedEffect = PathEffect::MakeCorner(_radius);
  }
  if (_cachedEffect == nullptr) {
    return;
  }
  for (auto& shape : context->shapes) {
    shape = Shape::ApplyEffect(shape, _cachedEffect);
  }
}

}  // namespace tgfx
