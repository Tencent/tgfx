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
#include <array>
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

static float BlendStrokeWidth(float base, const GlyphStyle& style) {
  if (style.strokeWidthFactor <= 0.0f) {
    return base;
  }
  if (style.strokeWidthFactor >= 1.0f) {
    return style.strokeWidth;
  }
  return base + (style.strokeWidth - base) * style.strokeWidthFactor;
}

struct BlendPaintInfo {
  std::shared_ptr<Shader> shader = nullptr;
  float alpha = 1.0f;
  BlendMode blendMode = BlendMode::SrcOver;
};

static std::array<BlendPaintInfo, 2> MakeBlendPaints(const std::shared_ptr<Shader>& baseShader,
                                                     float baseAlpha, BlendMode baseBlendMode,
                                                     const GlyphStyle& style) {
  std::array<BlendPaintInfo, 2> paints = {};
  float blendFactor = style.strokeColor.alpha;
  float runAlpha = baseAlpha * style.alpha;

  if (blendFactor < 1.0f) {
    paints[0].shader = baseShader;
    paints[0].alpha = runAlpha;
    paints[0].blendMode = baseBlendMode;
  }
  if (blendFactor > 0.0f) {
    auto color =
        Color{style.strokeColor.red, style.strokeColor.green, style.strokeColor.blue, blendFactor};
    paints[1].shader = Shader::MakeColorShader(color);
    paints[1].alpha = runAlpha;
    paints[1].blendMode = BlendMode::SrcOver;
  }
  return paints;
}

class StrokePainter : public Painter {
 public:
  Stroke stroke = {};
  std::shared_ptr<PathEffect> pathEffect = nullptr;
  std::vector<Matrix> innerMatrices = {};

  std::unique_ptr<Painter> clone() const override {
    return std::make_unique<StrokePainter>(*this);
  }

  void draw(LayerRecorder* recorder) override {
    for (size_t i = 0; i < geometries.size(); i++) {
      auto* geometry = geometries[i];
      const auto& innerMatrix = innerMatrices[i];

      Matrix invertedInner = Matrix::I();
      bool canInvert = innerMatrix.invert(&invertedInner);
      auto outerMatrix = canInvert ? geometry->matrix * invertedInner : Matrix::I();
      auto scales = outerMatrix.getAxisScales();
      bool uniformScale = FloatNearlyEqual(scales.x, scales.y);

      if (geometry->hasText()) {
        for (const auto& run : geometry->getGlyphRuns()) {
          if (uniformScale && pathEffect == nullptr) {
            drawGlyphRunAsTextBlob(recorder, run, geometry->matrix, scales.x);
          } else {
            drawGlyphRunAsShape(recorder, run, innerMatrix, outerMatrix, scales, uniformScale);
          }
        }
      } else {
        drawShape(recorder, geometry->getShape(), innerMatrix, outerMatrix, scales, uniformScale);
      }
    }
  }

 private:
  void drawShape(LayerRecorder* recorder, std::shared_ptr<Shape> shape, const Matrix& innerMatrix,
                 const Matrix& outerMatrix, const Point& scales, bool uniformScale) {
    if (shape == nullptr) {
      return;
    }
    Matrix finalOuter = outerMatrix;
    if (innerMatrix.isTranslate()) {
      finalOuter.preTranslate(innerMatrix.getTranslateX(), innerMatrix.getTranslateY());
    } else {
      shape = Shape::ApplyMatrix(shape, innerMatrix);
    }
    if (pathEffect) {
      shape = Shape::ApplyEffect(shape, pathEffect);
    }
    if (uniformScale) {
      shape = Shape::ApplyMatrix(shape, finalOuter);
      LayerPaint paint(shader, alpha, blendMode);
      paint.style = PaintStyle::Stroke;
      paint.stroke = stroke;
      paint.stroke.width *= scales.x;
      recorder->addShape(std::move(shape), paint);
    } else {
      shape = Shape::ApplyStroke(shape, &stroke);
      if (shape == nullptr) {
        return;
      }
      shape = Shape::ApplyMatrix(shape, finalOuter);
      LayerPaint paint(shader, alpha, blendMode);
      recorder->addShape(std::move(shape), paint);
    }
  }

  void drawGlyphRunAsTextBlob(LayerRecorder* recorder, const StyledGlyphRun& run,
                              const Matrix& matrix, float scale) {
    Stroke runStroke = stroke;
    runStroke.width = BlendStrokeWidth(stroke.width, run.style) * scale;
    auto paints = MakeBlendPaints(shader, alpha, blendMode, run.style);

    for (const auto& info : paints) {
      if (info.shader == nullptr) {
        continue;
      }
      LayerPaint paint(info.shader, info.alpha, info.blendMode);
      paint.style = PaintStyle::Stroke;
      paint.stroke = runStroke;
      recorder->addTextBlob(run.textBlob, paint, matrix);
    }
  }

  void drawGlyphRunAsShape(LayerRecorder* recorder, const StyledGlyphRun& run,
                           const Matrix& innerMatrix, const Matrix& outerMatrix,
                           const Point& scales, bool uniformScale) {
    auto shape = Shape::MakeFrom(run.textBlob);
    if (shape == nullptr) {
      return;
    }
    if (!innerMatrix.isTranslate()) {
      shape = Shape::ApplyMatrix(shape, innerMatrix);
    }
    if (pathEffect) {
      shape = Shape::ApplyEffect(shape, pathEffect);
    }

    Stroke runStroke = stroke;
    runStroke.width = BlendStrokeWidth(stroke.width, run.style);
    auto paints = MakeBlendPaints(shader, alpha, blendMode, run.style);

    if (uniformScale) {
      shape = Shape::ApplyMatrix(shape, outerMatrix);
      runStroke.width *= scales.x;
      for (const auto& info : paints) {
        if (info.shader == nullptr) {
          continue;
        }
        LayerPaint paint(info.shader, info.alpha, info.blendMode);
        paint.style = PaintStyle::Stroke;
        paint.stroke = runStroke;
        recorder->addShape(shape, paint);
      }
    } else {
      shape = Shape::ApplyStroke(shape, &runStroke);
      if (shape == nullptr) {
        return;
      }
      shape = Shape::ApplyMatrix(shape, outerMatrix);
      for (const auto& info : paints) {
        if (info.shader == nullptr) {
          continue;
        }
        LayerPaint paint(info.shader, info.alpha, info.blendMode);
        recorder->addShape(shape, paint);
      }
    }
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
  painter->geometries.reserve(context->geometries.size());
  painter->innerMatrices.reserve(context->geometries.size());
  for (auto& geometry : context->geometries) {
    painter->geometries.push_back(geometry.get());
    painter->innerMatrices.push_back(geometry->matrix);
  }
  painter->stroke = _stroke;
  if (_cachedDashEffect == nullptr && !_dashes.empty()) {
    _cachedDashEffect = CreateDashEffect(_dashes, _dashOffset);
  }
  painter->pathEffect = _cachedDashEffect;
  context->painters.push_back(std::move(painter));
}

}  // namespace tgfx
