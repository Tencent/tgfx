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

#include "tgfx/layers/DisplayList.h"

namespace tgfx {
Layer* DisplayList::root() const {
  Layer* root = nullptr;
  auto animation = Animation::Basic(&Layer::setAlpha);
  animation->from = 1.0f;
  animation->to = 0.5f;

  auto animation2 = Animation::Keyframe(&Layer::setBlendMode);
  animation2->values = {BlendMode::ColorDodge};
  return root;
}
}  // namespace tgfx
