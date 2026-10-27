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
//  license is distributed on an "AS IS" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <optional>
#include "core/DrawContext.h"
#include "core/utils/MathExtra.h"
#include "tgfx/core/Brush.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Picture.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/core/TextBlob.h"
#include "tgfx/layers/layerstyles/StyledShape.h"

namespace tgfx {

/**
 * Reads back the shape a layer's geometry recording covers by playing its picture back, instead of
 * re-deriving it from the elements behind it. Any draw that is not the same outline rejects the
 * extraction, so callers fall back to a non-vector path rather than covering part of the layer.
 */
class GeometryShapeContext : public DrawContext, public Picture::AbortCallback {
 public:
  /**
   * Returns the single shape the picture covers, already in layer coordinates, or nullopt when the
   * picture paints anything beyond that one outline. contentScale is the scale the recording canvas
   * was scaled by.
   */
  static std::optional<StyledShape> Extract(const std::shared_ptr<Picture>& picture,
                                            float contentScale) {
    if (picture == nullptr || FloatNearlyZero(contentScale)) {
      return std::nullopt;
    }
    GeometryShapeContext context = {};
    context.scale = contentScale;
    // The same object aborts the playback: once a draw rejects the extraction no later record can
    // add a shape back, so the rest of the picture does not have to be read.
    picture->playback(&context, Matrix::I(), ClipStack(), &context);
    return context.finish();
  }

  bool abort() override {
    return state == State::Rejected;
  }

  void drawFill(const Brush&) override {
    reject();
  }

  void drawRect(const Rect& rect, const Matrix& matrix, const ClipStack& clip, const Brush& brush,
                const Stroke* stroke) override {
    if (!accept(brush, clip, stroke)) {
      return;
    }
    Path path = {};
    path.addRect(rect);
    addPath(std::move(path), matrix, nullptr);
  }

  void drawRRect(const RRect& rRect, const Matrix& matrix, const ClipStack& clip,
                 const Brush& brush, const Stroke* stroke) override {
    if (!accept(brush, clip, stroke)) {
      return;
    }
    Path path = {};
    path.addRRect(rRect);
    addPath(std::move(path), matrix, nullptr);
  }

  void drawPath(const Path& path, const Matrix& matrix, const ClipStack& clip,
                const Brush& brush) override {
    if (!accept(brush, clip, nullptr)) {
      return;
    }
    addPath(path, matrix, nullptr);
  }

  void drawShape(std::shared_ptr<Shape> shape, const Matrix& matrix, const ClipStack& clip,
                 const Brush& brush, const Stroke* stroke) override {
    if (!accept(brush, clip, stroke)) {
      return;
    }
    // The same shape instance under the same matrix repeats the outline we already hold, so the
    // path never has to be built again for the painters sharing one geometry.
    if (shape != nullptr && candidate.source == shape && candidate.matrix == matrix) {
      return;
    }
    if (shape == nullptr) {
      reject();
      return;
    }
    addPath(shape->getPath(), matrix, std::move(shape));
  }

  void drawMesh(std::shared_ptr<Mesh>, const Matrix&, const ClipStack&, const Brush&) override {
    // Mesh coverage comes from its vertices, and a mesh cannot be sampled as one outline.
    reject();
  }

  void drawImage(std::shared_ptr<Image>, const SamplingOptions&, const Matrix&, const ClipStack&,
                 const Brush&) override {
    reject();
  }

  void drawImageRect(std::shared_ptr<Image>, const Rect&, const Rect&, const SamplingOptions&,
                     const Matrix&, const ClipStack&, const Brush&, SrcRectConstraint,
                     const Rect*) override {
    reject();
  }

  void drawTextBlob(std::shared_ptr<TextBlob> textBlob, const Matrix& matrix, const ClipStack& clip,
                    const Brush& brush, const Stroke* stroke) override {
    if (!accept(brush, clip, stroke)) {
      return;
    }
    if (textBlob == nullptr) {
      reject();
      return;
    }
    // Color glyphs paint without outline coverage, so a path built from the rest would cover less
    // than the recording paints.
    for (auto run : *textBlob) {
      if (!(run.font.hasOutlines() && !run.font.hasColor())) {
        reject();
        return;
      }
    }
    auto shape = Shape::MakeFrom(textBlob);
    if (shape == nullptr) {
      reject();
      return;
    }
    addPath(shape->getPath(), matrix, std::move(shape));
  }

  void drawPicture(std::shared_ptr<Picture> picture, const Matrix& matrix,
                   const ClipStack& clip) override {
    if (state == State::Rejected || picture == nullptr) {
      return;
    }
    // A nested picture only adds draw records to the same recording, so playback continues in this
    // context with the state it was drawn under.
    picture->playback(this, matrix, clip, this);
  }

  void drawLayer(std::shared_ptr<Picture>, std::shared_ptr<ImageFilter>, const Matrix&,
                 const ClipStack&, const Brush&) override {
    // The nested picture goes through an offscreen layer, so its pixels are not this geometry.
    reject();
  }

 private:
  // NoCandidate: nothing has been seen yet. Candidate: one outline is held and every draw so far
  // has confirmed it. Rejected: a draw could not be expressed as that one outline, and a rejection
  // is final -- no later draw can turn the picture back into a single shape.
  enum class State { NoCandidate, Candidate, Rejected };

  struct Candidate {
    Path path = {};
    Matrix matrix = Matrix::I();
    std::shared_ptr<Shape> source = nullptr;
  };

  void reject() {
    state = State::Rejected;
  }

  /**
   * Returns false when this draw cannot contribute to a single shape. Every reason here is about
   * the pixels not matching the fill path: a stroke widens the outline (and inside/outside
   * alignment even clips it), a clip keeps only part of it, and an image shader or a mask filter
   * changes the coverage the brush produces.
   */
  bool accept(const Brush& brush, const ClipStack& clip, const Stroke* stroke) {
    if (state == State::Rejected) {
      return false;
    }
    if (stroke != nullptr) {
      reject();
      return false;
    }
    if (clip.state() != ClipState::WideOpen) {
      reject();
      return false;
    }
    if (brush.shader != nullptr || brush.maskFilter != nullptr) {
      reject();
      return false;
    }
    return true;
  }

  void addPath(Path path, const Matrix& matrix, std::shared_ptr<Shape> source) {
    if (state == State::Rejected) {
      return;
    }
    if (path.isEmpty()) {
      reject();
      return;
    }
    // Inverse coverage is the complement of the outline, so it is not the area the layer covers.
    if (path.isInverseFillType()) {
      reject();
      return;
    }
    path.transform(toLayerMatrix(matrix));
    if (state == State::NoCandidate) {
      candidate.path = std::move(path);
      candidate.matrix = matrix;
      candidate.source = std::move(source);
      state = State::Candidate;
      return;
    }
    // Two draws disagreeing on the outline cannot be expressed as one shape. Comparing the paths
    // rather than their bounds keeps shapes covering the same box apart, which a bounds test would
    // silently accept as coverage the layer never painted.
    if (!(path == candidate.path)) {
      reject();
    }
  }

  // The recording canvas is scaled by contentScale, so undo it to land in layer coordinates. The
  // image offset keeps the scaled coordinates; mixing the two would move the shape off the pixels.
  Matrix toLayerMatrix(const Matrix& matrix) const {
    Matrix result = Matrix::MakeScale(1.0f / scale);
    result.preConcat(matrix);
    return result;
  }

  std::optional<StyledShape> finish() {
    if (state != State::Candidate) {
      return std::nullopt;
    }
    auto shape = Shape::MakeFrom(std::move(candidate.path));
    if (shape == nullptr) {
      return std::nullopt;
    }
    return StyledShape::Make(std::move(shape), StyledShapeType::Fill, 0.0f, StrokeAlign::Center);
  }

  State state = State::NoCandidate;
  Candidate candidate = {};
  float scale = 1.0f;
};

}  // namespace tgfx
