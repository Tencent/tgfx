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
#include "tgfx/layers/LayerRecorder.h"

namespace tgfx {

void Painter::captureGeometries(VectorContext* context) {
  geometries.reserve(context->geometries.size());
  innerMatrices.reserve(context->geometries.size());
  for (auto& geometry : context->geometries) {
    geometries.push_back(geometry.get());
    innerMatrices.push_back(geometry->matrix);
  }
}

void Painter::draw(LayerRecorder* recorder) {
  for (size_t i = 0; i < geometries.size(); i++) {
    auto* geometry = geometries[i];
    const auto& innerMatrix = innerMatrices[i];
    auto outerMatrix = computeOuterMatrix(i);

    if (geometry->hasText()) {
      for (const auto& run : geometry->getGlyphRuns()) {
        auto emit = prepareGlyphRun(run, i);
        if (emit.paints.empty()) {
          continue;
        }
        auto runMatrix = run.matrix;
        runMatrix.postConcat(innerMatrix);
        runMatrix.postConcat(outerMatrix);
        recorder->setMatrix(runMatrix);
        for (const auto& paint : emit.paints) {
          if (emit.shape != nullptr) {
            recorder->addShape(emit.shape, paint);
          } else if (emit.textBlob != nullptr) {
            recorder->addTextBlob(emit.textBlob, paint);
          }
        }
        recorder->resetMatrix();
      }
      continue;
    }

    auto shape = geometry->getShape();
    if (shape == nullptr) {
      continue;
    }
    shape = Shape::ApplyMatrix(shape, innerMatrix);
    if (shape == nullptr) {
      // innerMatrix is non-invertible (e.g. a degenerate scale); skip to avoid handing a null
      // shape to prepareShape, which subclasses are allowed to dereference.
      continue;
    }
    auto paint = makeBasePaint();
    shape = prepareShape(std::move(shape), i, &paint);
    if (shape == nullptr) {
      continue;
    }
    recorder->setMatrix(outerMatrix);
    recorder->addShape(std::move(shape), paint);
    recorder->resetMatrix();
  }
}

std::shared_ptr<Shader> Painter::wrapShaderWithFit(const Rect& innerBounds) const {
  if (colorSource == nullptr || !colorSource->fitsToGeometry()) {
    return shader;
  }
  return shader->makeWithMatrix(colorSource->getFitMatrix(innerBounds));
}

LayerPaint Painter::makeBasePaint() const {
  LayerPaint paint = {};
  paint.blendMode = blendMode;
  paint.placement = placement;
  paint.color.alpha = alpha;
  return paint;
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

}  // namespace tgfx
