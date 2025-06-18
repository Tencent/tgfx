/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "PlaybackContext.h"
#include "DrawContext.h"

namespace tgfx {
PlaybackContext::PlaybackContext(MCState state, const FillModifier* fillModifier)
    : initState(state), fillModifier(fillModifier), _state(std::move(state)) {
  hasInitMatrix = !initState.matrix.isIdentity();
  hasInitClip = !initState.clip.isEmpty() || !initState.clip.isInverseFillType();
}

void PlaybackContext::setMatrix(const Matrix& matrix) {
  _state.matrix = matrix;
  if (hasInitMatrix) {
    _state.matrix.postConcat(initState.matrix);
  }
}

void PlaybackContext::setClip(const Path& clip) {
  _state.clip = clip;
  if (hasInitMatrix) {
    _state.clip.transform(initState.matrix);
  }
  if (hasInitClip) {
    _state.clip.addPath(initState.clip, PathOp::Intersect);
  }
}

void PlaybackContext::setColor(const Color& color) {
  if (fillModifier) {
    lastOriginalFill.color = color;
    _fill = fillModifier->transform(lastOriginalFill);
  } else {
    _fill.color = color;
  }
}

void PlaybackContext::setFill(const Fill& fill) {
  if (fillModifier) {
    lastOriginalFill = fill;
    _fill = fillModifier->transform(lastOriginalFill);
  } else {
    _fill = fill;
  }
}

void PlaybackContext::setStrokeWidth(float width) {
  _stroke.width = width;
  hasStroke = true;
}

void PlaybackContext::setStroke(const Stroke& stroke) {
  _stroke = stroke;
  hasStroke = true;
}

void PlaybackContext::setHasStroke(bool value) {
  hasStroke = value;
}

void PlaybackContext::drawFill(DrawContext* context) {
  if (hasInitClip) {
    context->drawPath(initState.clip, {}, _fill.makeWithMatrix(initState.matrix));
  } else if (hasInitMatrix) {
    context->drawFill(_fill.makeWithMatrix(initState.matrix));
  } else {
    context->drawFill(_fill);
  }
}

}  // namespace tgfx
