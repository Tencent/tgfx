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

#include "tgfx/layers/vectors/TextModifier.h"
#include "core/utils/Log.h"
#include "tgfx/layers/Layer.h"

namespace tgfx {

void TextModifier::setSelectors(std::vector<std::shared_ptr<TextSelector>> value) {
  for (const auto& owner : owners) {
    for (const auto& selector : _selectors) {
      DEBUG_ASSERT(selector != nullptr);
      selector->detachFromLayer(owner);
    }
    for (const auto& selector : value) {
      DEBUG_ASSERT(selector != nullptr);
      selector->attachToLayer(owner);
    }
  }
  _selectors = std::move(value);
  invalidateContent();
}

void TextModifier::attachToLayer(Layer* layer) {
  VectorElement::attachToLayer(layer);
  for (const auto& selector : _selectors) {
    DEBUG_ASSERT(selector != nullptr);
    selector->attachToLayer(layer);
  }
}

void TextModifier::detachFromLayer(Layer* layer) {
  for (const auto& selector : _selectors) {
    DEBUG_ASSERT(selector != nullptr);
    selector->detachFromLayer(layer);
  }
  VectorElement::detachFromLayer(layer);
}

void TextModifier::setAnchorPoint(Point value) {
  if (_anchorPoint == value) {
    return;
  }
  _anchorPoint = value;
  invalidateContent();
}

void TextModifier::setPosition(Point value) {
  if (_position == value) {
    return;
  }
  _position = value;
  invalidateContent();
}

void TextModifier::setScale(Point value) {
  if (_scale == value) {
    return;
  }
  _scale = value;
  invalidateContent();
}

void TextModifier::setSkew(float value) {
  if (_skew == value) {
    return;
  }
  _skew = value;
  invalidateContent();
}

void TextModifier::setSkewAxis(float value) {
  if (_skewAxis == value) {
    return;
  }
  _skewAxis = value;
  invalidateContent();
}

void TextModifier::setRotation(float value) {
  if (_rotation == value) {
    return;
  }
  _rotation = value;
  invalidateContent();
}

void TextModifier::setAlpha(float value) {
  if (_alpha == value) {
    return;
  }
  _alpha = value;
  invalidateContent();
}

void TextModifier::setFillColor(std::optional<Color> value) {
  if (_fillColor == value) {
    return;
  }
  _fillColor = value;
  invalidateContent();
}

void TextModifier::setStrokeColor(std::optional<Color> value) {
  if (_strokeColor == value) {
    return;
  }
  _strokeColor = value;
  invalidateContent();
}

void TextModifier::setStrokeWidth(std::optional<float> value) {
  if (_strokeWidth == value) {
    return;
  }
  _strokeWidth = value;
  invalidateContent();
}

void TextModifier::apply(VectorContext*) {
}

}  // namespace tgfx
