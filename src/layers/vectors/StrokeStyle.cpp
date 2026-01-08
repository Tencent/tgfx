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

#include "tgfx/layers/vectors/StrokeStyle.h"
#include "Painter.h"
#include "VectorContext.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "tgfx/core/PathEffect.h"
#include "tgfx/layers/LayerPaint.h"
#include "tgfx/layers/LayerRecorder.h"

namespace tgfx {

static std::shared_ptr<PathEffect> CreateDashEffect(const std::vector<float>& dashes,
                                                    float dashOffset) {
  if (dashes.empty()) {
    return nullptr;
  }
  auto dashCount = dashes.size();
  std::vector<float> dashList = dashes;
  if (dashCount % 2 == 1) {
    dashList.insert(dashList.end(), dashes.begin(), dashes.end());
  }
  return PathEffect::MakeDash(dashList.data(), static_cast<int>(dashList.size()), dashOffset);
}

class StrokePainter : public Painter {
 public:
  Stroke stroke = {};
  std::shared_ptr<PathEffect> pathEffect = nullptr;

  std::unique_ptr<Painter> clone() const override {
    return std::make_unique<StrokePainter>(*this);
  }

 protected:
  void onDraw(LayerRecorder* recorder, Geometry* geometry, const Matrix& innerMatrix) override {
    LayerPaint paint(shader, alpha, blendMode);
    auto scales = matrix.getAxisScales();
    bool uniformScale = FloatNearlyEqual(scales.x, scales.y);

    // For TextBlob with uniform scale and no dash effect, draw text directly
    auto textBlob = geometry->getTextBlob();
    if (textBlob && uniformScale && pathEffect == nullptr) {
      auto finalMatrix = innerMatrix;
      finalMatrix.postConcat(matrix);
      paint.style = PaintStyle::Stroke;
      paint.stroke = stroke;
      paint.stroke.width *= scales.x;
      recorder->addTextBlob(textBlob, paint, finalMatrix);
      return;
    }

    // Fall back to Shape: for ShapeGeometry, non-uniform scale, or dash effect
    auto shape = geometry->getShape();
    if (shape == nullptr) {
      return;
    }

    // Apply inner matrix before dash effect, unless it's translation-only
    auto outerMatrix = matrix;
    if (innerMatrix.isTranslate()) {
      // Merge translation into outer matrix
      outerMatrix.preTranslate(innerMatrix.getTranslateX(), innerMatrix.getTranslateY());
    } else {
      shape = Shape::ApplyMatrix(shape, innerMatrix);
    }

    // Apply dash effect if present
    if (pathEffect) {
      shape = Shape::ApplyEffect(shape, pathEffect);
    }

    if (uniformScale) {
      // Uniform scale: apply outer matrix to shape, scale stroke width
      shape = Shape::ApplyMatrix(shape, outerMatrix);
      paint.style = PaintStyle::Stroke;
      paint.stroke = stroke;
      paint.stroke.width *= scales.x;
    } else {
      // Non-uniform scale: apply stroke first, then outer matrix
      shape = Shape::ApplyStroke(shape, &stroke);
      if (shape == nullptr) {
        return;
      }
      shape = Shape::ApplyMatrix(shape, outerMatrix);
    }
    recorder->addShape(std::move(shape), paint);
  }
};

void StrokeStyle::setColorSource(std::shared_ptr<ColorSource> value) {
  if (_colorSource == value) {
    return;
  }
  replaceChildProperty(_colorSource.get(), value.get());
  _colorSource = std::move(value);
  invalidateContent();
}

void StrokeStyle::setAlpha(float value) {
  if (_alpha == value) {
    return;
  }
  _alpha = value;
  invalidateContent();
}

void StrokeStyle::setBlendMode(BlendMode value) {
  if (_blendMode == value) {
    return;
  }
  _blendMode = value;
  invalidateContent();
}

void StrokeStyle::setStrokeWidth(float value) {
  if (_stroke.width == value) {
    return;
  }
  _stroke.width = value;
  invalidateContent();
}

void StrokeStyle::setLineCap(LineCap value) {
  if (_stroke.cap == value) {
    return;
  }
  _stroke.cap = value;
  invalidateContent();
}

void StrokeStyle::setLineJoin(LineJoin value) {
  if (_stroke.join == value) {
    return;
  }
  _stroke.join = value;
  invalidateContent();
}

void StrokeStyle::setMiterLimit(float value) {
  if (_stroke.miterLimit == value) {
    return;
  }
  _stroke.miterLimit = value;
  invalidateContent();
}

void StrokeStyle::setDashes(std::vector<float> value) {
  if (_dashes == value) {
    return;
  }
  _dashes = std::move(value);
  _cachedDashEffect = nullptr;
  invalidateContent();
}

void StrokeStyle::setDashOffset(float value) {
  if (_dashOffset == value) {
    return;
  }
  _dashOffset = value;
  _cachedDashEffect = nullptr;
  invalidateContent();
}

void StrokeStyle::attachToLayer(Layer* layer) {
  VectorElement::attachToLayer(layer);
  if (_colorSource) {
    _colorSource->attachToLayer(layer);
  }
}

void StrokeStyle::detachFromLayer(Layer* layer) {
  VectorElement::detachFromLayer(layer);
  if (_colorSource) {
    _colorSource->detachFromLayer(layer);
  }
}

void StrokeStyle::apply(VectorContext* context) {
  DEBUG_ASSERT(context != nullptr);
  if (_colorSource == nullptr || _stroke.width <= 0.0f || context->geometries.empty()) {
    return;
  }
  auto shader = _colorSource->getShader();
  if (shader == nullptr) {
    return;
  }

  auto painter = std::make_unique<StrokePainter>();
  painter->shader = std::move(shader);
  painter->blendMode = _blendMode;
  painter->alpha = _alpha;
  painter->startIndex = 0;
  painter->matrices = context->matrices;
  painter->stroke = _stroke;
  if (_cachedDashEffect == nullptr && !_dashes.empty()) {
    _cachedDashEffect = CreateDashEffect(_dashes, _dashOffset);
  }
  painter->pathEffect = _cachedDashEffect;
  context->painters.push_back(std::move(painter));
}

}  // namespace tgfx
