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
#include "tgfx/core/PathTypes.h"
#include "tgfx/layers/LayerRecorder.h"

namespace tgfx {

static PathFillType ToPathFillType(FillRule fillRule) {
  switch (fillRule) {
    case FillRule::Winding:
      return PathFillType::Winding;
    case FillRule::EvenOdd:
      return PathFillType::EvenOdd;
  }
  return PathFillType::Winding;
}

class FillPainter : public Painter {
 public:
  PathFillType fillRule = PathFillType::Winding;

  std::unique_ptr<Painter> clone() const override {
    return std::make_unique<FillPainter>(*this);
  }

 protected:
  std::shared_ptr<Shape> prepareShape(std::shared_ptr<Shape> innerShape, size_t /*index*/,
                                      LayerPaint* paint) override {
    if (innerShape->fillType() == PathFillType::Winding) {
      innerShape = Shape::ApplyFillType(innerShape, fillRule);
    }
    // Use the resolved path bounds so the fit region matches the actual fill footprint rather than the conservative cover.
    paint->shader = wrapShaderWithFit(innerShape->getPath().getBounds());
    return innerShape;
  }

  GlyphRunEmit prepareGlyphRun(const StyledGlyphRun& run, size_t /*index*/) override {
    GlyphRunEmit emit = {};
    if (run.textBlob == nullptr) {
      return emit;
    }
    emit.textBlob = run.textBlob;
    // Tight bounds keep the fit region in step with the visible glyph extents instead of the conservative blob cover.
    auto baseShader = wrapShaderWithFit(run.textBlob->getTightBounds());
    float blendFactor = run.style.fillColor.alpha;
    float runAlpha = alpha * run.style.alpha;
    if (blendFactor < 1.0f) {
      auto paint = makeBasePaint();
      paint.color.alpha = runAlpha;
      paint.shader = baseShader;
      emit.paints.push_back(std::move(paint));
    }
    if (blendFactor > 0.0f) {
      const auto& fillColor = run.style.fillColor;
      auto overlayColor = Color{fillColor.red, fillColor.green, fillColor.blue, blendFactor};
      auto paint = makeBasePaint();
      paint.blendMode = BlendMode::SrcOver;
      paint.shader = Shader::MakeColorShader(overlayColor);
      paint.color.alpha = runAlpha;
      emit.paints.push_back(std::move(paint));
    }
    return emit;
  }
};

std::shared_ptr<FillStyle> FillStyle::Make(std::shared_ptr<ColorSource> colorSource) {
  if (colorSource == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<FillStyle>(new FillStyle(std::move(colorSource)));
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

void FillStyle::setFillRule(FillRule value) {
  if (_fillRule == value) {
    return;
  }
  _fillRule = value;
  invalidateContent();
}

void FillStyle::setPlacement(LayerPlacement value) {
  if (_placement == value) {
    return;
  }
  _placement = value;
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
  painter->colorSource = _colorSource;
  painter->blendMode = _blendMode;
  painter->alpha = _alpha;
  painter->placement = _placement;
  painter->fillRule = ToPathFillType(_fillRule);
  painter->captureGeometries(context);
  context->painters.push_back(std::move(painter));
}

}  // namespace tgfx
