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
#include "core/utils/Log.h"
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
  DEBUG_ASSERT(context != nullptr);
  if (context->geometries.empty()) {
    return;
  }
  if (_cachedEffect == nullptr) {
    _cachedEffect = PathEffect::MakeCorner(_radius);
  }
  if (_cachedEffect == nullptr) {
    return;
  }
  auto geometries = context->getShapeGeometries();
  for (auto& geometry : geometries) {
    geometry->shape = Shape::ApplyEffect(geometry->shape, _cachedEffect);
  }
}

}  // namespace tgfx
