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
#include "layers/DashEffect.h"
#include "tgfx/layers/LayerRecorder.h"

namespace tgfx {

static float BlendStrokeWidth(float base, const GlyphStyle& style) {
  if (style.strokeWidthFactor <= 0.0f) {
    return base;
  }
  if (style.strokeWidthFactor >= 1.0f) {
    return style.strokeWidth;
  }
  return base + (style.strokeWidth - base) * style.strokeWidthFactor;
}

class StrokePainter : public Painter {
 public:
  Stroke stroke = {};
  std::shared_ptr<PathEffect> pathEffect = nullptr;
  StrokeAlign strokeAlign = StrokeAlign::Center;

  std::unique_ptr<Painter> clone() const override {
    if (!canClone()) {
      return nullptr;
    }
    return std::make_unique<StrokePainter>(*this);
  }

 protected:
  std::shared_ptr<Shape> prepareShape(std::shared_ptr<Shape> innerShape, size_t /*index*/,
                                      LayerPaint* paint, VectorPaintSnapshot* snapshot) override {
    auto bounds = innerShape->getPath().getBounds();
    *snapshot = makeBaseSnapshot(bounds);
    paint->shader = wrapShaderWithFit(*snapshot);
    paint->style = PaintStyle::Stroke;
    paint->stroke = stroke;
    paint->strokeAlign = strokeAlign;
    paint->pathEffect = pathEffect;
    return innerShape;
  }

  GlyphRunEmit prepareGlyphRun(const StyledGlyphRun& run, size_t /*index*/) override {
    GlyphRunEmit emit = {};
    if (run.textBlob == nullptr) {
      return emit;
    }
    float blendFactor = run.style.strokeColor.alpha;
    size_t paintCount = (blendFactor < 1.0f ? 1 : 0) + (blendFactor > 0.0f ? 1 : 0);
    if (budget != nullptr && !budget->consumeDraws(paintCount)) {
      return emit;
    }
    Stroke runStroke = stroke;
    runStroke.width = BlendStrokeWidth(stroke.width, run.style);

    auto bounds = run.textBlob->getTightBounds();
    auto baseSnapshot = makeBaseSnapshot(bounds, run.style.alpha);
    if (budget != nullptr && budget->failed) {
      return {};
    }
    auto baseShader = wrapShaderWithFit(baseSnapshot);

    // Emit path: non-center alignment or an active path effect require converting the text blob
    // into a shape so that ShapeContent can drive the clip/expand pipeline. When neither applies
    // keep the blob intact to preserve color glyphs (e.g. emoji) that cannot be reduced to a
    // path.
    bool nonCenterAlign = strokeAlign != StrokeAlign::Center;
    if (pathEffect != nullptr || nonCenterAlign) {
      if (budget != nullptr && !budget->consumeTextGlyphs(run.textBlob.get())) {
        return {};
      }
      emit.shape = Shape::MakeFrom(run.textBlob);
      if (emit.shape == nullptr) {
        return emit;
      }
    } else {
      emit.textBlob = run.textBlob;
    }

    float runAlpha = alpha * run.style.alpha;
    if (blendFactor < 1.0f) {
      auto p = makeBasePaint();
      p.color.alpha = runAlpha;
      p.shader = baseShader;
      p.style = PaintStyle::Stroke;
      p.stroke = runStroke;
      p.strokeAlign = strokeAlign;
      p.pathEffect = pathEffect;
      emit.paints.push_back(std::move(p));
      emit.paintSnapshots.push_back(std::move(baseSnapshot));
    }
    if (blendFactor > 0.0f) {
      const auto& strokeColor = run.style.strokeColor;
      auto overlayColor = Color{strokeColor.red, strokeColor.green, strokeColor.blue, blendFactor};
      auto p = makeBasePaint();
      p.blendMode = BlendMode::SrcOver;
      p.shader = Shader::MakeColorShader(overlayColor);
      p.color.alpha = runAlpha;
      p.style = PaintStyle::Stroke;
      p.stroke = runStroke;
      p.strokeAlign = strokeAlign;
      p.pathEffect = pathEffect;
      emit.paints.push_back(std::move(p));
      emit.paintSnapshots.push_back(makeOverlaySnapshot(bounds, overlayColor, run.style.alpha));
    }
    return emit;
  }

 private:
  PainterStyle onGetStyle() const override {
    return {PaintStyle::Stroke, stroke.width, strokeAlign};
  }
};

std::shared_ptr<StrokeStyle> StrokeStyle::Make(std::shared_ptr<ColorSource> colorSource) {
  if (colorSource == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<StrokeStyle>(new StrokeStyle(std::move(colorSource)));
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

void StrokeStyle::setDashAdaptive(bool value) {
  if (_dashAdaptive == value) {
    return;
  }
  _dashAdaptive = value;
  _cachedDashEffect = nullptr;
  invalidateContent();
}

void StrokeStyle::setStrokeAlign(StrokeAlign value) {
  if (_strokeAlign == value) {
    return;
  }
  _strokeAlign = value;
  invalidateContent();
}

void StrokeStyle::setPlacement(LayerPlacement value) {
  if (_placement == value) {
    return;
  }
  _placement = value;
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
  if (!context->budget->consumePainters(1) || !context->budget->consumeMetadata(1) ||
      !context->budget->consumeColorSource(_colorSource.get()) ||
      (_cachedDashEffect == nullptr && !context->budget->consumeDashPattern(_dashes.size()))) {
    return;
  }
  auto shader = _colorSource->getShader();
  if (shader == nullptr) {
    return;
  }

  auto painter = std::make_unique<StrokePainter>();
  painter->shader = std::move(shader);
  painter->colorSource = _colorSource;
  painter->colorSnapshot = VectorColorSnapshot::Capture(_colorSource.get());
  painter->sourceTag = sourceTag();
  painter->runtimeEndpoint = this;
  painter->blendMode = _blendMode;
  painter->alpha = _alpha;
  painter->budget = context->budget.get();
  painter->alphaFactors.push_back({VectorAlphaFactor::Kind::Painter,
                                   sourceTag() != nullptr ? sourceTag() : this, _alpha, this});
  painter->placement = _placement;
  painter->stroke = _stroke;
  painter->strokeAlign = _strokeAlign;
  if (_cachedDashEffect == nullptr && !_dashes.empty()) {
    _cachedDashEffect = CreateDashPathEffect(_dashes, _dashOffset, _dashAdaptive, _stroke);
  }
  painter->pathEffect = _cachedDashEffect;
  painter->captureGeometries(context);
  if (context->budget->failed) {
    return;
  }
  context->painters.push_back(std::move(painter));
}

}  // namespace tgfx
