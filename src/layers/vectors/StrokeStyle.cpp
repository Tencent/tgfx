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

// Skip the boolean op for outside-align stroke when the original shape's bounds are sub-pixel
// thin (a degenerate-fallback rectangle/ellipse can have one axis at 5e-3). Skia PathOps is
// numerically unstable on such input and may emit spurious diagonals visualized as spikes.
// At sub-pixel scale the original shape is invisible anyway, so omitting the Difference is
// visually harmless.
static constexpr float MinBoundExtentForBooleanOp = 0.5f;

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
  std::vector<std::shared_ptr<Shape>> originalShapes = {};

  std::unique_ptr<Painter> clone() const override {
    return std::make_unique<StrokePainter>(*this);
  }

 protected:
  std::shared_ptr<Shape> prepareShape(std::shared_ptr<Shape> innerShape, size_t index,
                                      LayerPaint* paint) override {
    // Derive the fit shader from the pre-stroke geometry bounds. SVG and Figma both compute
    // gradient fit boxes from the fill geometry only and ignore stroke width, so dash, stroke
    // alignment and stroke width never affect the fit region. Reading getPath() here also
    // primes Shape::getPath()'s atomic cache for the recorder's later use.
    paint->shader = wrapShaderWithFit(innerShape->getPath().getBounds());

    if (pathEffect) {
      innerShape = Shape::ApplyEffect(innerShape, pathEffect);
      if (innerShape == nullptr) {
        return nullptr;
      }
    }
    if (strokeAlign != StrokeAlign::Center) {
      // prepareShape is only invoked for non-text geometries. StrokeStyle::apply fills
      // originalShapes with geometry->getShape() for those entries when strokeAlign is not
      // Center (text geometries push a null placeholder, but prepareGlyphRun handles text).
      DEBUG_ASSERT(originalShapes[index] != nullptr);
      auto originalShape = Shape::ApplyMatrix(originalShapes[index], innerMatrices[index]);
      innerShape = applyStrokeAndAlign(std::move(innerShape), std::move(originalShape), stroke);
      if (innerShape == nullptr) {
        return nullptr;
      }
    } else {
      paint->style = PaintStyle::Stroke;
      paint->stroke = stroke;
    }
    return innerShape;
  }

  GlyphRunEmit prepareGlyphRun(const StyledGlyphRun& run, size_t /*index*/) override {
    GlyphRunEmit emit = {};
    if (run.textBlob == nullptr) {
      return emit;
    }
    Stroke runStroke = stroke;
    runStroke.width = BlendStrokeWidth(stroke.width, run.style);

    // Tight glyph bounds keep color glyphs intact (no text-to-shape conversion for measurement).
    auto baseShader = wrapShaderWithFit(run.textBlob->getTightBounds());

    // Emit path: non-center alignment or an active path effect require expanding the text blob
    // into a shape (alignment needs a boolean op against the original outline, path effects
    // operate on path geometry). When neither applies keep the blob intact to preserve color
    // glyphs (e.g. emoji) that cannot be reduced to a path.
    std::shared_ptr<Shape> runShape = nullptr;
    bool needsBooleanOp = strokeAlign != StrokeAlign::Center;
    if (pathEffect != nullptr || needsBooleanOp) {
      runShape = Shape::MakeFrom(run.textBlob);
      // Snapshot the un-effected outline for the boolean-op step. Shape is immutable, so the
      // pre-effect shape remains a valid "original outline" and sharing it lets Shape::getPath()'s
      // atomic cache serve both the boolean op and any subsequent path consumer.
      auto originalShape = runShape;
      if (runShape != nullptr && pathEffect != nullptr) {
        runShape = Shape::ApplyEffect(runShape, pathEffect);
      }
      if (runShape != nullptr && needsBooleanOp) {
        runShape = applyStrokeAndAlign(std::move(runShape), std::move(originalShape), runStroke);
      }
      if (runShape == nullptr) {
        return emit;
      }
    }
    if (runShape != nullptr) {
      emit.shape = runShape;
    } else {
      emit.textBlob = run.textBlob;
    }

    float blendFactor = run.style.strokeColor.alpha;
    float runAlpha = alpha * run.style.alpha;
    if (blendFactor < 1.0f) {
      auto paint = makeBasePaint();
      paint.color.alpha = runAlpha;
      paint.shader = baseShader;
      if (!needsBooleanOp) {
        paint.style = PaintStyle::Stroke;
        paint.stroke = runStroke;
      }
      emit.paints.push_back(std::move(paint));
    }
    if (blendFactor > 0.0f) {
      const auto& strokeColor = run.style.strokeColor;
      auto overlayColor = Color{strokeColor.red, strokeColor.green, strokeColor.blue, blendFactor};
      auto paint = makeBasePaint();
      paint.blendMode = BlendMode::SrcOver;
      paint.shader = Shader::MakeColorShader(overlayColor);
      paint.color.alpha = runAlpha;
      if (!needsBooleanOp) {
        paint.style = PaintStyle::Stroke;
        paint.stroke = runStroke;
      }
      emit.paints.push_back(std::move(paint));
    }
    return emit;
  }

 private:
  std::shared_ptr<Shape> applyStrokeAndAlign(std::shared_ptr<Shape> shape,
                                             std::shared_ptr<Shape> originalShape,
                                             const Stroke& strokeToApply) const {
    Stroke doubled = strokeToApply;
    doubled.width *= 2;
    shape = Shape::ApplyStroke(shape, &doubled);
    if (shape == nullptr) {
      return nullptr;
    }
    auto op = strokeAlign == StrokeAlign::Inside ? PathOp::Intersect : PathOp::Difference;
    if (op == PathOp::Difference && originalShape != nullptr) {
      auto bounds = originalShape->getBounds();
      if (bounds.width() < MinBoundExtentForBooleanOp ||
          bounds.height() < MinBoundExtentForBooleanOp) {
        return shape;
      }
    }
    return Shape::Merge(std::move(shape), std::move(originalShape), op);
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
  auto shader = _colorSource->getShader();
  if (shader == nullptr) {
    return;
  }

  auto painter = std::make_unique<StrokePainter>();
  painter->shader = std::move(shader);
  painter->colorSource = _colorSource;
  painter->blendMode = _blendMode;
  painter->alpha = _alpha;
  painter->placement = _placement;
  painter->stroke = _stroke;
  painter->strokeAlign = _strokeAlign;
  if (_cachedDashEffect == nullptr && !_dashes.empty()) {
    _cachedDashEffect = CreateDashPathEffect(_dashes, _dashOffset, _dashAdaptive, _stroke);
  }
  painter->pathEffect = _cachedDashEffect;
  painter->captureGeometries(context);
  if (_strokeAlign != StrokeAlign::Center) {
    painter->originalShapes.reserve(context->geometries.size());
    for (auto& geometry : context->geometries) {
      // Text geometries are handled entirely by prepareGlyphRun, which rebuilds the original
      // outline from the run's textBlob. Push a null placeholder to keep index alignment with
      // geometries while avoiding an eager text-to-shape conversion that would also pollute
      // Geometry::shape cache.
      if (geometry->hasText()) {
        painter->originalShapes.push_back(nullptr);
        continue;
      }
      painter->originalShapes.push_back(geometry->getShape());
    }
  }
  context->painters.push_back(std::move(painter));
}

}  // namespace tgfx
