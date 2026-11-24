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

#include "Painter.h"
#include "VectorContext.h"
#include "core/utils/Types.h"
#include "tgfx/layers/vectors/Gradient.h"
#include "tgfx/layers/vectors/ImagePattern.h"
#include "tgfx/layers/vectors/SolidColor.h"

namespace tgfx {

VectorColorSnapshot VectorColorSnapshot::Capture(const ColorSource* source) {
  DEBUG_ASSERT(source != nullptr);
  VectorColorSnapshot snapshot = {};
  snapshot.fitsToGeometry = source->fitsToGeometry();
  switch (Types::Get(source)) {
    case Types::ColorSourceType::SolidColor:
      snapshot.color = static_cast<const SolidColor*>(source)->color();
      break;
    case Types::ColorSourceType::ImagePattern: {
      const auto* pattern = static_cast<const ImagePattern*>(source);
      snapshot.kind = Kind::ImagePattern;
      snapshot.image = pattern->image();
      snapshot.tileModeX = pattern->tileModeX();
      snapshot.tileModeY = pattern->tileModeY();
      snapshot.sampling = pattern->samplingOptions();
      snapshot.matrix = pattern->matrix();
      snapshot.scaleMode = pattern->scaleMode();
      break;
    }
    case Types::ColorSourceType::Gradient: {
      const auto* gradient = static_cast<const Gradient*>(source);
      snapshot.colors = gradient->colors();
      snapshot.positions = gradient->positions();
      snapshot.matrix = gradient->matrix();
      switch (gradient->type()) {
        case GradientType::None:
          DEBUG_ASSERT(false);
          break;
        case GradientType::Linear: {
          const auto* linear = static_cast<const LinearGradient*>(gradient);
          snapshot.kind = Kind::Linear;
          snapshot.start = linear->startPoint();
          snapshot.end = linear->endPoint();
          break;
        }
        case GradientType::Radial: {
          const auto* radial = static_cast<const RadialGradient*>(gradient);
          snapshot.kind = Kind::Radial;
          snapshot.center = radial->center();
          snapshot.radius = radial->radius();
          break;
        }
        case GradientType::Conic: {
          const auto* conic = static_cast<const ConicGradient*>(gradient);
          snapshot.kind = Kind::Conic;
          snapshot.center = conic->center();
          snapshot.startAngle = conic->startAngle();
          snapshot.endAngle = conic->endAngle();
          break;
        }
        case GradientType::Diamond: {
          const auto* diamond = static_cast<const DiamondGradient*>(gradient);
          snapshot.kind = Kind::Diamond;
          snapshot.center = diamond->center();
          snapshot.radius = diamond->radius();
          break;
        }
      }
      break;
    }
  }
  return snapshot;
}

void Painter::applyAlpha(float groupAlpha, const void* sourceTag, const void* runtimeEndpoint,
                         VectorAlphaFactor::Kind kind) {
  if (budget != nullptr && !budget->consumeMetadata(1)) {
    return;
  }
  alphaFactors.push_back({kind, sourceTag, groupAlpha, runtimeEndpoint});
  alpha *= groupAlpha;
}

void Painter::captureGeometries(VectorContext* context) {
  if (!context->budget->consumeCaptures(context->geometries.size()) ||
      !context->budget->consumeMetadata(context->geometries.size())) {
    return;
  }
  geometries.reserve(context->geometries.size());
  innerMatrices.reserve(context->geometries.size());
  for (auto& geometry : context->geometries) {
    geometries.push_back(geometry.get());
    innerMatrices.push_back(geometry->matrix);
  }
}

static void CaptureShaderFold(const LayerPaint& paint, VectorPaintSnapshot* snapshot) {
  Color color = Color::Transparent();
  snapshot->shaderIsSolid = paint.shader != nullptr && paint.shader->asColor(&color);
  snapshot->shaderSolidColor = snapshot->shaderIsSolid ? color : Color::Transparent();
}

void Painter::draw(VectorDrawSink* sink) {
  for (size_t i = 0; i < geometries.size(); i++) {
    if (budget != nullptr && budget->failed) {
      return;
    }
    auto* geometry = geometries[i];
    const auto& innerMatrix = innerMatrices[i];
    auto outerMatrix = computeOuterMatrix(i);
    if (geometry->hasText()) {
      const auto& runs = geometry->getGlyphRuns();
      for (size_t runIndex = 0; runIndex < runs.size(); runIndex++) {
        const auto& run = runs[runIndex];
        if (budget != nullptr && !budget->consumeTextPoints(run.textBlob.get(), nullptr)) {
          return;
        }
        auto emit = prepareGlyphRun(run, i);
        if (budget != nullptr && budget->failed) {
          return;
        }
        if (emit.paints.empty() || (emit.shape == nullptr && emit.textBlob == nullptr)) {
          continue;
        }
        DEBUG_ASSERT(emit.paints.size() == emit.paintSnapshots.size());
        for (size_t paintIndex = 0; paintIndex < emit.paints.size(); paintIndex++) {
          CaptureShaderFold(emit.paints[paintIndex], &emit.paintSnapshots[paintIndex]);
        }
        auto op = makeDrawOp(i);
        if (budget != nullptr && budget->failed) {
          return;
        }
        op.matrix = run.matrix;
        op.matrix.postConcat(innerMatrix);
        op.matrix.postConcat(outerMatrix);
        op.runOrdinal = runIndex;
        op.textBlob = std::move(emit.textBlob);
        op.shape = std::move(emit.shape);
        op.paints = std::move(emit.paints);
        op.paintSnapshots = std::move(emit.paintSnapshots);
        sink->drawGlyphRun(op);
      }
      continue;
    }
    if (budget != nullptr &&
        (!budget->consumeDraws(1) || !budget->consumePoints(geometry->pointCount))) {
      return;
    }
    auto shape = geometry->getShape();
    if (shape == nullptr) {
      continue;
    }
    shape = Shape::ApplyMatrix(shape, innerMatrix);
    if (shape == nullptr) {
      continue;
    }
    auto paint = makeBasePaint();
    VectorPaintSnapshot snapshot = {};
    shape = prepareShape(std::move(shape), i, &paint, &snapshot);
    if (budget != nullptr && budget->failed) {
      return;
    }
    if (shape == nullptr) {
      continue;
    }
    CaptureShaderFold(paint, &snapshot);
    auto op = makeDrawOp(i);
    if (budget != nullptr && budget->failed) {
      return;
    }
    op.shape = std::move(shape);
    op.matrix = outerMatrix;
    op.paints.push_back(std::move(paint));
    op.paintSnapshots.push_back(std::move(snapshot));
    sink->drawShape(op);
  }
}

std::shared_ptr<Shader> Painter::wrapShaderWithFit(const VectorPaintSnapshot& snapshot) const {
  if (!snapshot.colorSource.fitsToGeometry) {
    return shader;
  }
  return shader->makeWithMatrix(snapshot.fitMatrix);
}

bool Painter::canClone() const {
  return budget == nullptr ||
         (budget->consumePainters(1) && budget->consumeCaptures(geometries.size()) &&
          budget->consumeMetadata(innerMatrices.size()) &&
          budget->consumeMetadata(alphaFactors.size()) &&
          budget->consumePoints(colorSnapshot.colors.size()) &&
          budget->consumePoints(colorSnapshot.positions.size()));
}

LayerPaint Painter::makeBasePaint() const {
  LayerPaint paint = {};
  paint.blendMode = blendMode;
  paint.placement = placement;
  paint.color.alpha = alpha;
  return paint;
}

VectorPaintSnapshot Painter::makeBaseSnapshot(const Rect& bounds, float glyphAlpha) const {
  VectorPaintSnapshot snapshot = {};
  if (budget != nullptr &&
      (!budget->consumePoints(colorSnapshot.colors.size()) ||
       !budget->consumePoints(colorSnapshot.positions.size()) ||
       !budget->consumeMetadata(alphaFactors.size()) || !budget->consumeMetadata(1))) {
    return snapshot;
  }
  snapshot.colorSource = colorSnapshot;
  snapshot.alphaFactors = alphaFactors;
  snapshot.scopePainterAlpha = alpha;
  snapshot.glyphAlpha = glyphAlpha;
  snapshot.fitBounds = bounds;
  if (colorSnapshot.fitsToGeometry) {
    snapshot.fitMatrix = colorSource->getFitMatrix(bounds);
  }
  return snapshot;
}

VectorPaintSnapshot Painter::makeOverlaySnapshot(const Rect& bounds, const Color& color,
                                                 float glyphAlpha) const {
  VectorPaintSnapshot snapshot = {};
  if (budget != nullptr &&
      (!budget->consumeMetadata(alphaFactors.size()) || !budget->consumeMetadata(1))) {
    return snapshot;
  }
  snapshot.alphaFactors = alphaFactors;
  snapshot.role = VectorPaintSnapshot::Role::Overlay;
  snapshot.colorSource.color = color;
  snapshot.scopePainterAlpha = alpha;
  snapshot.glyphAlpha = glyphAlpha;
  snapshot.overrideWeight = color.alpha;
  snapshot.fitBounds = bounds;
  return snapshot;
}

VectorDrawOp Painter::makeDrawOp(size_t index) const {
  const auto* geometry = geometries[index];
  VectorDrawOp op = {};
  if (budget != nullptr) {
    if (!budget->consumeMetadata(geometry->copyProvenance.size()) ||
        !budget->consumeMetadata(geometry->sourceProvenance.size()) ||
        !budget->consumeMetadata(geometry->dependencies.size())) {
      return op;
    }
    for (const auto& origin : geometry->sourceProvenance) {
      if (!budget->consumeMetadata(origin.copyProvenance.size())) {
        return op;
      }
    }
  }
  op.dependencies = geometry->dependencies;
  op.geometry = geometry;
  op.geometryOrdinal = geometry->ordinal;
  op.sourceTag = geometry->sourceTag;
  op.sourceEndpoint = geometry->sourceEndpoint;
  op.sourceProvenance = geometry->sourceProvenance;
  op.copyProvenance = geometry->copyProvenance;
  op.painterTag = sourceTag;
  op.painterEndpoint = runtimeEndpoint;
  op.colorSource = colorSource;
  op.capturedInnerMatrix = innerMatrices[index];
  op.finalMatrix = geometry->matrix;
  return op;
}

Matrix Painter::computeOuterMatrix(size_t index) const {
  Matrix inverted = Matrix::I();
  if (!innerMatrices[index].invert(&inverted)) {
    return Matrix::I();
  }
  auto outer = inverted;
  outer.postConcat(geometries[index]->matrix);
  return outer;
}

PainterStyle Painter::getStyle() const {
  return onGetStyle();
}

}  // namespace tgfx
