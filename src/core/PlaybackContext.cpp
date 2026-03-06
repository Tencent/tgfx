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
    : initMatrix(matrix), initClipStack(clip) {
  hasInitMatrix = !initMatrix.isIdentity();
  hasInitClip = initClipStack.state() != ClipState::WideOpen;
  _matrix = initMatrix;
  _clipStack = initClipStack;
}

void PlaybackContext::setMatrix(const Matrix& matrix) {
  _matrix = matrix;
  if (hasInitMatrix) {
    _matrix.postConcat(initMatrix);
  }
}

void PlaybackContext::setClip(const ClipRecord& record, const std::vector<ClipElement>& elements) {
  _clipStack.reset(record, elements);
  if (hasInitMatrix) {
    // Transform clip elements by init matrix.
    // Note: ClipStack stores transformed paths, so we need to transform them again.
    auto& clipElements = const_cast<std::vector<ClipElement>&>(_clipStack.elements());
    for (auto& element : clipElements) {
      auto transformedPath = element.getPath();
      transformedPath.transform(initMatrix);
      element = ClipElement(transformedPath, element.isAntiAlias());
    }
  }
  if (hasInitClip) {
    // Intersect with init clip by adding all init clip elements.
    auto& initElements = initClipStack.elements();
    for (size_t i = initClipStack.oldestValidIndex(); i < initElements.size(); ++i) {
      if (initElements[i].isValid()) {
        _clipStack.clip(initElements[i].getPath(), initElements[i].isAntiAlias());
      }
    }
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
    // Draw each clip element as a fill path.
    auto brush = _brush.makeWithMatrix(initMatrix);
    auto& initElements = initClipStack.elements();
    for (size_t i = initClipStack.oldestValidIndex(); i < initElements.size(); ++i) {
      if (initElements[i].isValid()) {
        brush.antiAlias = initElements[i].isAntiAlias();
        context->drawPath(initElements[i].getPath(), Matrix::I(), _clipStack, brush);
      }
    }
  } else if (hasInitMatrix) {
    context->drawFill(_brush.makeWithMatrix(initMatrix));
  } else {
    context->drawFill(_brush);
  }
}

}  // namespace tgfx
