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

#include "TransformContext.h"
#include "core/utils/Log.h"

namespace tgfx {
TransformContext::TransformContext(DrawContext* drawContext, const MCState& state)
    : drawContext(drawContext), initState(state) {
  DEBUG_ASSERT(drawContext != nullptr);
  auto hasClip = !state.clip.isEmpty() || !state.clip.isInverseFillType();
  auto hasMatrix = !state.matrix.isIdentity();
  if (hasClip && hasMatrix) {
    _type = Type::Both;
  } else if (hasClip) {
    _type = Type::Clip;
  } else if (hasMatrix) {
    _type = Type::Matrix;
  } else {
    _type = Type::None;
  }
}

MCState TransformContext::transform(const MCState& state) {
  auto newState = state;
  if (_type == Type::Matrix || _type == Type::Both) {
    newState.matrix.postConcat(initState.matrix);
    newState.clip.transform(initState.matrix);
  }
  if (_type == Type::Clip || _type == Type::Both) {
    if (newState.clip != lastClip) {
      lastClip = newState.clip;
      lastIntersectedClip = initState.clip;
      lastIntersectedClip.addPath(newState.clip, PathOp::Intersect);
    }
    newState.clip = lastIntersectedClip;
  }
  return newState;
}
}  // namespace tgfx
