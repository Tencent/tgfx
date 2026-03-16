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

#include "PlaybackContext.h"
#include "DrawContext.h"

namespace tgfx {
PlaybackContext::PlaybackContext(const Matrix& matrix, const ClipStack& clip)
    : initMatrix(matrix), initClip(clip) {
  hasInitMatrix = !initMatrix.isIdentity();
  hasInitClip = initClip.state() != ClipState::WideOpen;
  _matrix = initMatrix;
  _clip = initClip;
}

void PlaybackContext::setMatrix(const Matrix& matrix) {
  _matrix = matrix;
  if (hasInitMatrix) {
    _matrix.postConcat(initMatrix);
  }
}

void PlaybackContext::setClip(const ClipStack& clip) {
  if (!hasInitMatrix && !hasInitClip) {
    _clip = clip;
    return;
  }

  if (!hasInitClip) {
    _clip = clip;
    _clip.transform(initMatrix);
    return;
  }

  _clip = initClip;
  auto& elements = clip.elements();
  for (size_t i = clip.oldestValidIndex(); i < elements.size(); ++i) {
    const auto& element = elements[i];
    if (!element.isValid()) {
      continue;
    }
    auto path = element.path();
    if (hasInitMatrix) {
      path.transform(initMatrix);
    }
    _clip.clip(path, element.isAntiAlias());
  }
}

void PlaybackContext::setColor(const Color& color) {
  _brush.color = color;
}

void PlaybackContext::setBrush(const Brush& brush) {
  _brush = brush;
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
    // Use an empty inverse path to fill the entire area, clipped by the current clip.
    Path path = {};
    path.setFillType(PathFillType::InverseWinding);
    context->drawPath(path, Matrix::I(), _clip, _brush.makeWithMatrix(initMatrix));
  } else if (hasInitMatrix) {
    context->drawFill(_brush.makeWithMatrix(initMatrix));
  } else {
    context->drawFill(_brush);
  }
}

}  // namespace tgfx
