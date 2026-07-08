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

#include "tgfx/layers/layerstyles/LayerStyle.h"

namespace tgfx {
void LayerStyle::setBlendMode(BlendMode blendMode) {
  if (_blendMode == blendMode) {
    return;
  }
  _blendMode = blendMode;
  invalidateTransform();
}

Rect LayerStyle::filterBackground(const Rect& srcRect, float) {
  return srcRect;
}

void LayerStyle::setExcludeChildEffects(bool value) {
  if (_excludeChildEffects == value) {
    return;
  }
  _excludeChildEffects = value;
  invalidateTransform();
}

}  // namespace tgfx
