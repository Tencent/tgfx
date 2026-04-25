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

// Expands the supplied pre-stroke bounds into the bounds that the stroked shape will actually
// occupy. The computation follows the stroke alignment semantics directly instead of relying on
// post-stroke or boolean-op shape bounds, which are conservative and may drift far from reality
// for degenerate shapes or merge operations. Miter spikes are intentionally ignored so the fit
// region tracks the main stroke band rather than stretching along theoretical sharp corners.
static void ExpandBoundsForStroke(Rect* bounds, const Stroke& stroke, StrokeAlign align) {
  if (bounds == nullptr) {
    return;
  }
  if (align == StrokeAlign::Inside) {
    // Inside stroke lives entirely within the original shape; the fit region is the shape itself.
    return;
  }
  auto factor = align == StrokeAlign::Outside ? 1.0f : 0.5f;
  auto expand = ceilf(stroke.width * factor);
  bounds->outset(expand, expand);
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
    if (pathEffect) {
      innerShape = Shape::ApplyEffect(innerShape, pathEffect);
      if (innerShape == nullptr) {
        return nullptr;
      }
    }
    // The fit region must describe the visible stroked area, which cannot be read back from the
    // post-stroke (or boolean-op) shape bounds: stroke expansion and merge operations both yield
    // shape objects whose reported bounds are conservative approximations. Compute the region
    // directly from the pre-stroke bounds and the stroke parameters per alignment.
    auto fitBounds = innerShape->getBounds();
    ExpandBoundsForStroke(&fitBounds, stroke, strokeAlign);

    if (strokeAlign != StrokeAlign::Center) {
      auto originalShape = Shape::ApplyMatrix(originalShapes[index], innerMatrices[index]);
      innerShape = applyStrokeAndAlign(std::move(innerShape), std::move(originalShape), stroke);
      if (innerShape == nullptr) {
        return nullptr;
      }
    } else {
      paint->style = PaintStyle::Stroke;
      paint->stroke = stroke;
    }
    paint->shader = wrapShaderWithFit(fitBounds);
    return innerShape;
  }

  std::vector<GlyphEmit> prepareGlyphRun(const StyledGlyphRun& run, size_t /*index*/) override {
    std::vector<GlyphEmit> emits = {};
    if (run.textBlob == nullptr) {
      return emits;
    }
    Stroke runStroke = stroke;
    runStroke.width = BlendStrokeWidth(stroke.width, run.style);

    Rect fitBounds = run.textBlob->getBounds();
    ExpandBoundsForStroke(&fitBounds, runStroke, strokeAlign);
    auto baseShader = wrapShaderWithFit(fitBounds);

    // Non-center stroke alignment or an active path effect (e.g. dashing) both require expanding
    // the text blob into a shape: alignment needs a boolean op against the original outline, and
    // path effects operate on path geometry. When neither applies keep the blob intact to
    // preserve color glyphs (e.g. emoji) that cannot be reduced to a path.
    std::shared_ptr<Shape> runShape = nullptr;
    bool needsBooleanOp = strokeAlign != StrokeAlign::Center;
    if (pathEffect != nullptr || needsBooleanOp) {
      runShape = Shape::MakeFrom(run.textBlob);
      if (runShape != nullptr && pathEffect != nullptr) {
        runShape = Shape::ApplyEffect(runShape, pathEffect);
      }
      if (runShape != nullptr && needsBooleanOp) {
        auto originalShape = Shape::MakeFrom(run.textBlob);
        runShape = applyStrokeAndAlign(std::move(runShape), std::move(originalShape), runStroke);
      }
      if (runShape == nullptr) {
        return emits;
      }
    }

    // When boolean-op stroke alignment has already produced a filled outline, emit it as a fill;
    // otherwise the paint keeps the stroke so the stroker (or path-effected shape stroker) draws
    // it at render time.
    bool emitsAsFill = needsBooleanOp;

    float blendFactor = run.style.strokeColor.alpha;
    float runAlpha = alpha * run.style.alpha;
    if (blendFactor < 1.0f) {
      auto paint = makeBasePaint();
      paint.color.alpha = runAlpha;
      paint.shader = baseShader;
      if (!emitsAsFill) {
        paint.style = PaintStyle::Stroke;
        paint.stroke = runStroke;
      }
      GlyphEmit emit = {};
      emit.paint = paint;
      if (runShape != nullptr) {
        emit.shape = runShape;
      } else {
        emit.textBlob = run.textBlob;
      }
      emits.push_back(std::move(emit));
    }
    if (blendFactor > 0.0f) {
      const auto& strokeColor = run.style.strokeColor;
      auto overlayColor = Color{strokeColor.red, strokeColor.green, strokeColor.blue, blendFactor};
      LayerPaint overlay = {};
      overlay.blendMode = BlendMode::SrcOver;
      overlay.placement = placement;
      overlay.shader = Shader::MakeColorShader(overlayColor);
      overlay.color.alpha = runAlpha;
      if (!emitsAsFill) {
        overlay.style = PaintStyle::Stroke;
        overlay.stroke = runStroke;
      }
      GlyphEmit emit = {};
      emit.paint = overlay;
      if (runShape != nullptr) {
        emit.shape = runShape;
      } else {
        emit.textBlob = run.textBlob;
      }
      emits.push_back(std::move(emit));
    }
    return emits;
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
