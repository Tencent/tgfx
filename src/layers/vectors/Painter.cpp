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

class RecorderPainterSink : public PainterSink {
 public:
  explicit RecorderPainterSink(LayerRecorder* recorder) : _recorder(recorder) {
  }

  void addShape(std::shared_ptr<Shape> shape, const LayerPaint& paint, const Matrix& matrix,
                const Geometry* /*source*/, std::shared_ptr<ColorSource> /*colorSource*/) override {
    _recorder->setMatrix(matrix);
    _recorder->addShape(std::move(shape), &paint);
    _recorder->resetMatrix();
  }

  void addGlyphRun(std::shared_ptr<TextBlob> textBlob, std::shared_ptr<Shape> shape,
                   std::vector<LayerPaint> paints, const Matrix& matrix, const Geometry* /*source*/,
                   std::shared_ptr<ColorSource> /*colorSource*/) override {
    _recorder->setMatrix(matrix);
    for (const auto& paint : paints) {
      if (shape != nullptr) {
        _recorder->addShape(shape, &paint);
      } else if (textBlob != nullptr) {
        _recorder->addTextBlob(textBlob, &paint);
      }
    }
    _recorder->resetMatrix();
  }

 private:
  LayerRecorder* _recorder = nullptr;
};

void Painter::captureGeometries(VectorContext* context) {
  geometries.reserve(context->geometries.size());
  innerMatrices.reserve(context->geometries.size());
  for (auto& geometry : context->geometries) {
    geometries.push_back(geometry.get());
    innerMatrices.push_back(geometry->matrix);
  }
}

void Painter::draw(LayerRecorder* recorder) {
  RecorderPainterSink sink(recorder);
  draw(&sink);
}

void Painter::draw(PainterSink* sink) {
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
        sink->addGlyphRun(std::move(emit.textBlob), std::move(emit.shape), std::move(emit.paints),
                          runMatrix, geometry, colorSource);
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
    sink->addShape(std::move(shape), paint, outerMatrix, geometry, colorSource);
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

PainterStyle Painter::getStyle() const {
  return onGetStyle();
}

}  // namespace tgfx
