/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#pragma once

#include "core/DrawContext.h"
#include "core/PlaybackContext.h"

namespace tgfx {
enum class RecordType {
  SetMatrix,
  SetClip,
  SetColor,
  SetFill,
  SetStrokeWidth,
  SetStroke,
  SetHasStroke,
  DrawFill,
  DrawRect,
  DrawRRect,
  DrawPath,
  DrawShape,
  DrawImage,
  DrawImageRect,
  DrawImageRectToRect,
  DrawGlyphRunList,
  DrawPicture,
  DrawLayer
};

class Record {
 public:
  virtual ~Record() = default;

  virtual RecordType type() const = 0;

  virtual bool hasUnboundedFill(bool& /*hasInverseClip*/) const {
    return false;
  }

  virtual void playback(DrawContext* context, PlaybackContext* playback) const = 0;
};

class SetMatrix : public Record {
 public:
  explicit SetMatrix(const Matrix& matrix) : matrix(matrix) {
  }

  RecordType type() const override {
    return RecordType::SetMatrix;
  }

  void playback(DrawContext*, PlaybackContext* playback) const override {
    playback->setMatrix(matrix);
  }

  Matrix matrix = {};
};

class SetClip : public Record {
 public:
  explicit SetClip(Path clip) : clip(std::move(clip)) {
  }

  RecordType type() const override {
    return RecordType::SetClip;
  }

  bool hasUnboundedFill(bool& hasInverseClip) const override {
    hasInverseClip = clip.isInverseFillType();
    return false;
  }

  void playback(DrawContext*, PlaybackContext* playback) const override {
    playback->setClip(clip);
  }

  Path clip = {};
};

class SetColor : public Record {
 public:
  explicit SetColor(Color color) : color(color) {
  }

  RecordType type() const override {
    return RecordType::SetColor;
  }

  void playback(DrawContext*, PlaybackContext* playback) const override {
    playback->setColor(color);
  }

  Color color = {};
};

class SetFill : public Record {
 public:
  explicit SetFill(Fill fill) : fill(std::move(fill)) {
  }

  RecordType type() const override {
    return RecordType::SetFill;
  }

  void playback(DrawContext*, PlaybackContext* playback) const override {
    playback->setFill(fill);
  }

  Fill fill = {};
};

class SetStrokeWidth : public Record {
 public:
  explicit SetStrokeWidth(float width) : width(width) {
  }

  RecordType type() const override {
    return RecordType::SetStrokeWidth;
  }

  void playback(DrawContext*, PlaybackContext* playback) const override {
    playback->setStrokeWidth(width);
  }

  float width = 0.0f;
};

class SetStroke : public Record {
 public:
  explicit SetStroke(const Stroke& stroke) : stroke(stroke) {
  }

  RecordType type() const override {
    return RecordType::SetStroke;
  }

  void playback(DrawContext*, PlaybackContext* playback) const override {
    playback->setStroke(stroke);
  }

  Stroke stroke = {};
};

class SetHasStroke : public Record {
 public:
  explicit SetHasStroke(bool hasStroke) : hasStroke(hasStroke) {
  }

  RecordType type() const override {
    return RecordType::SetHasStroke;
  }

  void playback(DrawContext*, PlaybackContext* playback) const override {
    playback->setHasStroke(hasStroke);
  }

  bool hasStroke = false;
};

class DrawFill : public Record {
 public:
  RecordType type() const override {
    return RecordType::DrawFill;
  }

  bool hasUnboundedFill(bool& hasInverseClip) const override {
    return hasInverseClip;
  }

  void playback(DrawContext* context, PlaybackContext* playback) const override {
    playback->drawFill(context);
  }
};

class DrawRect : public Record {
 public:
  explicit DrawRect(const Rect& rect) : rect(rect) {
  }

  RecordType type() const override {
    return RecordType::DrawRect;
  }

  void playback(DrawContext* context, PlaybackContext* playback) const override {
    context->drawRect(rect, playback->state(), playback->fill(), nullptr);
  }

  Rect rect;
};

class DrawRRect : public Record {
 public:
  explicit DrawRRect(const RRect& rRect) : rRect(rRect) {
  }

  RecordType type() const override {
    return RecordType::DrawRRect;
  }

  void playback(DrawContext* context, PlaybackContext* playback) const override {
    context->drawRRect(rRect, playback->state(), playback->fill(), playback->stroke());
  }

  RRect rRect;
};

class DrawPath : public Record {
 public:
  explicit DrawPath(Path path) : path(std::move(path)) {
  }

  RecordType type() const override {
    return RecordType::DrawPath;
  }

  bool hasUnboundedFill(bool& hasInverseClip) const override {
    return hasInverseClip && path.isInverseFillType();
  }

  void playback(DrawContext* context, PlaybackContext* playback) const override {
    context->drawPath(path, playback->state(), playback->fill());
  }

  Path path;
};

class DrawShape : public Record {
 public:
  explicit DrawShape(std::shared_ptr<Shape> shape) : shape(std::move(shape)) {
  }

  RecordType type() const override {
    return RecordType::DrawShape;
  }

  bool hasUnboundedFill(bool& hasInverseClip) const override {
    return hasInverseClip && shape->isInverseFillType();
  }

  void playback(DrawContext* context, PlaybackContext* playback) const override {
    context->drawShape(shape, playback->state(), playback->fill(), playback->stroke());
  }

  std::shared_ptr<Shape> shape;
};

class DrawImage : public Record {
 public:
  DrawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling)
      : image(std::move(image)), sampling(sampling) {
  }

  RecordType type() const override {
    return RecordType::DrawImage;
  }

  void playback(DrawContext* context, PlaybackContext* playback) const override {
    context->drawImage(image, sampling, playback->state(), playback->fill());
  }

  std::shared_ptr<Image> image;
  SamplingOptions sampling;
};

class DrawImageRect : public DrawImage {
 public:
  DrawImageRect(std::shared_ptr<Image> image, const Rect& rect, const SamplingOptions& sampling,
                SrcRectConstraint constraint)
      : DrawImage(std::move(image), sampling), rect(rect), constraint(constraint) {
  }

  RecordType type() const override {
    return RecordType::DrawImageRect;
  }

  void playback(DrawContext* context, PlaybackContext* playback) const override {
    context->drawImageRect(image, rect, rect, sampling, playback->state(), playback->fill(),
                           constraint);
  }

  Rect rect;
  SrcRectConstraint constraint = SrcRectConstraint::Fast;
};

class DrawImageRectToRect : public DrawImageRect {
 public:
  DrawImageRectToRect(std::shared_ptr<Image> image, const Rect& srcRect, const Rect& dstRect,
                      const SamplingOptions& sampling, SrcRectConstraint constraint)
      : DrawImageRect(std::move(image), srcRect, sampling, constraint), dstRect(dstRect) {
  }

  RecordType type() const override {
    return RecordType::DrawImageRectToRect;
  }

  void playback(DrawContext* context, PlaybackContext* playback) const override {
    context->drawImageRect(image, rect, dstRect, sampling, playback->state(), playback->fill(),
                           constraint);
  }

  Rect dstRect;
};

class DrawGlyphRunList : public Record {
 public:
  explicit DrawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList)
      : glyphRunList(std::move(glyphRunList)) {
  }

  RecordType type() const override {
    return RecordType::DrawGlyphRunList;
  }

  void playback(DrawContext* context, PlaybackContext* playback) const override {
    context->drawGlyphRunList(glyphRunList, playback->state(), playback->fill(),
                              playback->stroke());
  }

  std::shared_ptr<GlyphRunList> glyphRunList;
};

class DrawPicture : public Record {
 public:
  explicit DrawPicture(std::shared_ptr<Picture> picture) : picture(std::move(picture)) {
  }

  RecordType type() const override {
    return RecordType::DrawPicture;
  }

  bool hasUnboundedFill(bool& hasInverseClip) const override {
    return hasInverseClip && picture->hasUnboundedFill();
  }

  void playback(DrawContext* context, PlaybackContext* playback) const override {
    context->drawPicture(picture, playback->state());
  }

  std::shared_ptr<Picture> picture;
};

class DrawLayer : public Record {
 public:
  DrawLayer(std::shared_ptr<Picture> picture, std::shared_ptr<ImageFilter> filter)
      : picture(std::move(picture)), filter(std::move(filter)) {
  }

  RecordType type() const override {
    return RecordType::DrawLayer;
  }

  bool hasUnboundedFill(bool& hasInverseClip) const override {
    return hasInverseClip && picture->hasUnboundedFill();
  }

  void playback(DrawContext* context, PlaybackContext* playback) const override {
    context->drawLayer(picture, filter, playback->state(), playback->fill());
  }

  std::shared_ptr<Picture> picture;
  std::shared_ptr<ImageFilter> filter;
};
}  // namespace tgfx
