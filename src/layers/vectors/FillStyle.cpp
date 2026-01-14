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

  void draw(LayerRecorder* recorder) override {
    for (auto* geometry : geometries) {
      if (geometry->hasText()) {
        for (const auto& run : geometry->getGlyphRuns()) {
          drawGlyphRun(recorder, geometry->matrix, run);
        }
        continue;
      }

      auto shape = geometry->getShape();
      if (shape == nullptr) {
        continue;
      }
      if (shape->fillType() == PathFillType::Winding) {
        shape = Shape::ApplyFillType(shape, fillRule);
      }
      shape = Shape::ApplyMatrix(shape, geometry->matrix);
      LayerPaint paint(shader, alpha, blendMode);
      recorder->addShape(std::move(shape), paint);
    }
  }

 private:
  void drawGlyphRun(LayerRecorder* recorder, const Matrix& geometryMatrix,
                    const StyledGlyphRun& run) {
    float blendFactor = run.style.fillColor.alpha;

    if (blendFactor < 1.0f) {
      LayerPaint paint(shader, alpha * run.style.alpha, blendMode);
      recorder->addTextBlob(run.textBlob, paint, geometryMatrix);
    }

    if (blendFactor > 0.0f) {
      const auto& fillColor = run.style.fillColor;
      auto overlayColor = Color{fillColor.red, fillColor.green, fillColor.blue, blendFactor};
      auto colorShader = Shader::MakeColorShader(overlayColor);
      LayerPaint paint(colorShader, alpha * run.style.alpha, BlendMode::SrcOver);
      recorder->addTextBlob(run.textBlob, paint, geometryMatrix);
    }
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
  painter->geometries.reserve(context->geometries.size());
  for (auto& geometry : context->geometries) {
    painter->geometries.push_back(geometry.get());
  }
  context->painters.push_back(std::move(painter));
}

}  // namespace tgfx
