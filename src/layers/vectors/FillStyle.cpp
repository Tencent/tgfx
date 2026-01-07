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

#include "tgfx/layers/vectors/FillStyle.h"
#include "Painter.h"
#include "VectorContext.h"
#include "core/utils/Log.h"
#include "tgfx/layers/LayerPaint.h"
#include "tgfx/layers/LayerRecorder.h"

namespace tgfx {

class FillPainter : public Painter {
 public:
  PathFillType fillRule = PathFillType::Winding;

  std::unique_ptr<Painter> clone() const override {
    return std::make_unique<FillPainter>(*this);
  }

 protected:
  void onDraw(LayerRecorder* recorder, Geometry* geometry, const Matrix& innerMatrix) override {
    auto finalMatrix = innerMatrix;
    finalMatrix.postConcat(matrix);
    LayerPaint paint(shader, alpha, blendMode);

    // Prefer drawing TextBlob directly for better rendering quality
    auto textBlob = geometry->getTextBlob();
    if (textBlob) {
      recorder->addTextBlob(textBlob, paint, finalMatrix);
      return;
    }

    // Fall back to Shape for ShapeGeometry or converted TextGeometry
    auto shape = geometry->getShape();
    if (shape == nullptr) {
      return;
    }
    // Only apply fillRule if the shape's current fillType is the default (Winding).
    // This preserves fillType set by PathOp (e.g., MergePath with XOR produces EvenOdd).
    if (shape->fillType() == PathFillType::Winding) {
      shape = Shape::ApplyFillType(shape, fillRule);
    }
    shape = Shape::ApplyMatrix(shape, finalMatrix);
    recorder->addShape(std::move(shape), paint);
  }
};

void FillStyle::setColorSource(std::shared_ptr<ColorSource> value) {
  if (_colorSource == value) {
    return;
  }
  replaceChildProperty(_colorSource.get(), value.get());
  _colorSource = std::move(value);
  invalidateContent();
}

void FillStyle::setAlpha(float value) {
  if (_alpha == value) {
    return;
  }
  _alpha = value;
  invalidateContent();
}

void FillStyle::setBlendMode(BlendMode value) {
  if (_blendMode == value) {
    return;
  }
  _blendMode = value;
  invalidateContent();
}

void FillStyle::setFillRule(PathFillType value) {
  if (_fillRule == value) {
    return;
  }
  _fillRule = value;
  invalidateContent();
}

void FillStyle::attachToLayer(Layer* layer) {
  VectorElement::attachToLayer(layer);
  if (_colorSource) {
    _colorSource->attachToLayer(layer);
  }
}

void FillStyle::detachFromLayer(Layer* layer) {
  VectorElement::detachFromLayer(layer);
  if (_colorSource) {
    _colorSource->detachFromLayer(layer);
  }
}

void FillStyle::apply(VectorContext* context) {
  DEBUG_ASSERT(context != nullptr);
  if (_colorSource == nullptr || context->geometries.empty()) {
    return;
  }
  auto shader = _colorSource->getShader();
  if (shader == nullptr) {
    return;
  }

  auto painter = std::make_unique<FillPainter>();
  painter->shader = std::move(shader);
  painter->blendMode = _blendMode;
  painter->alpha = _alpha;
  painter->fillRule = _fillRule;
  painter->startIndex = 0;
  painter->matrices = context->matrices;
  context->painters.push_back(std::move(painter));
}

}  // namespace tgfx
