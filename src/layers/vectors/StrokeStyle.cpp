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
#include <cmath>
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
  std::vector<std::shared_ptr<Shape>> originalShapes = {};

  std::unique_ptr<Painter> clone() const override {
    return std::make_unique<StrokePainter>(*this);
  }

 protected:
  std::shared_ptr<Shape> prepareShape(std::shared_ptr<Shape> innerShape, size_t index,
                                      LayerPaint* paint) override {
    // Derive the fit shader from the pre-effect, pre-align-bake shape. Dash and stroke alignment
    // only subset the pixels that the stroke band covers, so extending the original path bounds
    // by the stroke width is an exact upper bound for every effect/align combination. Reading
    // getPath() here also primes Shape::getPath()'s atomic cache for the recorder's later use.
    paint->shader = buildFitShader(innerShape, stroke.width);

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

    // Fit bounds come from the original TextBlob plus stroke width + align, matching the Shape
    // branch above. Dash and non-center alignment only choose which subset of the stroke band
    // is emitted; they do not enlarge it.
    auto baseShader = buildFitShader(run.textBlob, runStroke.width);

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
  // Builds the fit shader for a Shape drawable. The fit region is the pre-stroke path bounds
  // expanded by the stroke width according to strokeAlign. getPath().getBounds() returns tight
  // bounds for every Shape flavor (including MergeShape where onGetBounds is conservative) and
  // shares its atomic cache with the later rasterization path.
  //
  // miter spikes from miterLimit are ignored so the Shape and TextBlob paths follow the same
  // formula; on sharp miter joins the fit region may clip the tip colors by a fraction of a
  // pixel. This is a deliberate trade-off to avoid synthesizing a stroke-expanded Shape just
  // for measurement.
  //
  // Line-segment paths (e.g. degenerate Rectangles or hand-built ShapePath lineTo) use a
  // line-aware expansion so the fit band hugs the actual stroked pixels rather than the
  // axis-symmetric outset that area shapes need.
  std::shared_ptr<Shader> buildFitShader(const std::shared_ptr<Shape>& shape,
                                         float strokeWidth) const {
    if (colorSource == nullptr || !colorSource->fitsToGeometry() || shape == nullptr) {
      return shader;
    }
    const auto& path = shape->getPath();
    auto bounds = path.getBounds();
    Point linePoints[2] = {};
    if (path.isLine(linePoints)) {
      expandFitForLine(&bounds, strokeWidth, linePoints);
    } else {
      expandFitForArea(&bounds, strokeWidth);
    }
    return wrapShaderWithFit(bounds);
  }

  // Same contract as the Shape overload, sourced from the blob's tight glyph bounds so color
  // glyphs stay intact (no text-to-shape conversion for measurement). Text never collapses to
  // a single line segment so the area expansion always applies here.
  std::shared_ptr<Shader> buildFitShader(const std::shared_ptr<TextBlob>& textBlob,
                                         float strokeWidth) const {
    if (colorSource == nullptr || !colorSource->fitsToGeometry() || textBlob == nullptr) {
      return shader;
    }
    auto bounds = textBlob->getTightBounds();
    expandFitForArea(&bounds, strokeWidth);
    return wrapShaderWithFit(bounds);
  }

  // Outset per side for each stroke alignment (miter spikes ignored): Inside keeps the bounds,
  // Center grows by half the width, Outside grows by the full width. ceilf snaps the outset to
  // whole pixels so gradient stops land on pixel boundaries.
  void expandFitForArea(Rect* bounds, float strokeWidth) const {
    float outset = 0.0f;
    switch (strokeAlign) {
      case StrokeAlign::Inside:
        return;
      case StrokeAlign::Center:
        outset = ceilf(strokeWidth * 0.5f);
        break;
      case StrokeAlign::Outside:
        outset = ceilf(strokeWidth);
        break;
    }
    bounds->outset(outset, outset);
  }

  // Line-segment fit expansion. Perpendicular to the segment we mirror the area rule
  // (Center=half, Outside=full, Inside=zero since a line has no interior). Along the segment
  // we only extend when LineCap actually pushes pixels past the endpoints (Round/Square add
  // strokeWidth/2 regardless of strokeAlign; Butt adds nothing). Axis-aligned segments take
  // the precise per-axis outset; oblique segments (only reachable via ShapePath, since
  // Rectangle never produces them) use the conservative max so the band always covers the
  // rotated stroke without solving the projected envelope analytically.
  void expandFitForLine(Rect* bounds, float strokeWidth, const Point points[2]) const {
    if (strokeAlign == StrokeAlign::Inside) {
      return;
    }
    float perp =
        (strokeAlign == StrokeAlign::Outside) ? ceilf(strokeWidth) : ceilf(strokeWidth * 0.5f);
    float along = (stroke.cap == LineCap::Round || stroke.cap == LineCap::Square)
                      ? ceilf(strokeWidth * 0.5f)
                      : 0.0f;
    float dx = std::fabs(points[1].x - points[0].x);
    float dy = std::fabs(points[1].y - points[0].y);
    if (dy == 0.0f) {
      bounds->outset(along, perp);  // horizontal segment
    } else if (dx == 0.0f) {
      bounds->outset(perp, along);  // vertical segment
    } else {
      float m = std::max(perp, along);
      bounds->outset(m, m);  // oblique fallback (conservative)
    }
  }

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
