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
enum class PictureRecordType {
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

class PictureRecord {
 public:
  virtual ~PictureRecord() = default;

  virtual PictureRecordType type() const = 0;

  virtual bool hasUnboundedFill(bool& /*hasInverseClip*/) const {
    return false;
  }

  virtual void playback(DrawContext* context, PlaybackContext* playback) const = 0;
};

class SetMatrix : public PictureRecord {
 public:
  explicit SetMatrix(const Matrix& matrix) : matrix(matrix) {
  }

  PictureRecordType type() const override {
    return PictureRecordType::SetMatrix;
  }

  void playback(DrawContext*, PlaybackContext* playback) const override {
    playback->setMatrix(matrix);
  }

  Matrix matrix = {};
};

class SetClip : public PictureRecord {
 public:
  explicit SetClip(Path clip) : clip(std::move(clip)) {
  }

  PictureRecordType type() const override {
    return PictureRecordType::SetClip;
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

class SetColor : public PictureRecord {
 public:
  explicit SetColor(Color color) : color(color) {
  }

  PictureRecordType type() const override {
    return PictureRecordType::SetColor;
  }

  void playback(DrawContext*, PlaybackContext* playback) const override {
    playback->setColor(color);
  }

  Color color = {};
};

class SetBrush : public PictureRecord {
 public:
  explicit SetBrush(Brush brush) : brush(std::move(brush)) {
  }

  PictureRecordType type() const override {
    return PictureRecordType::SetFill;
  }

  void playback(DrawContext*, PlaybackContext* playback) const override {
    playback->setBrush(brush);
  }

  Brush brush = {};
};

class SetStrokeWidth : public PictureRecord {
 public:
  explicit SetStrokeWidth(float width) : width(width) {
  }

  PictureRecordType type() const override {
    return PictureRecordType::SetStrokeWidth;
  }

  void playback(DrawContext*, PlaybackContext* playback) const override {
    playback->setStrokeWidth(width);
  }

  float width = 0.0f;
};

class SetStroke : public PictureRecord {
 public:
  explicit SetStroke(const Stroke& stroke) : stroke(stroke) {
  }

  PictureRecordType type() const override {
    return PictureRecordType::SetStroke;
  }

  void playback(DrawContext*, PlaybackContext* playback) const override {
    playback->setStroke(stroke);
  }

  Stroke stroke = {};
};

class SetHasStroke : public PictureRecord {
 public:
  explicit SetHasStroke(bool hasStroke) : hasStroke(hasStroke) {
  }

  PictureRecordType type() const override {
    return PictureRecordType::SetHasStroke;
  }

  void playback(DrawContext*, PlaybackContext* playback) const override {
    playback->setHasStroke(hasStroke);
  }

  bool hasStroke = false;
};

class DrawFill : public PictureRecord {
 public:
  PictureRecordType type() const override {
    return PictureRecordType::DrawFill;
  }

  bool hasUnboundedFill(bool& hasInverseClip) const override {
    return hasInverseClip;
  }

  void playback(DrawContext* context, PlaybackContext* playback) const override {
    playback->drawFill(context);
  }
};

class DrawRect : public PictureRecord {
 public:
  explicit DrawRect(const Rect& rect) : rect(rect) {
  }

  PictureRecordType type() const override {
    return PictureRecordType::DrawRect;
  }

  void playback(DrawContext* context, PlaybackContext* playback) const override {
    context->drawRect(rect, playback->state(), playback->brush(), playback->stroke());
  }

  Rect rect;
};

class DrawRRect : public PictureRecord {
 public:
  explicit DrawRRect(const RRect& rRect) : rRect(rRect) {
  }

  PictureRecordType type() const override {
    return PictureRecordType::DrawRRect;
  }

  void playback(DrawContext* context, PlaybackContext* playback) const override {
    context->drawRRect(rRect, playback->state(), playback->brush(), playback->stroke());
  }

  RRect rRect;
};

class DrawPath : public PictureRecord {
 public:
  explicit DrawPath(Path path) : path(std::move(path)) {
  }

  PictureRecordType type() const override {
    return PictureRecordType::DrawPath;
  }

  bool hasUnboundedFill(bool& hasInverseClip) const override {
    return hasInverseClip && path.isInverseFillType();
  }

  void playback(DrawContext* context, PlaybackContext* playback) const override {
    context->drawPath(path, playback->state(), playback->brush());
  }

  Path path;
};

class DrawShape : public PictureRecord {
 public:
  explicit DrawShape(std::shared_ptr<Shape> shape) : shape(std::move(shape)) {
  }

  PictureRecordType type() const override {
    return PictureRecordType::DrawShape;
  }

  bool hasUnboundedFill(bool& hasInverseClip) const override {
    return hasInverseClip && shape->isInverseFillType();
  }

  void playback(DrawContext* context, PlaybackContext* playback) const override {
    context->drawShape(shape, playback->state(), playback->brush(), playback->stroke());
  }

  std::shared_ptr<Shape> shape;
};

class DrawImage : public PictureRecord {
 public:
  DrawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling)
      : image(std::move(image)), sampling(sampling) {
  }

  PictureRecordType type() const override {
    return PictureRecordType::DrawImage;
  }

  void playback(DrawContext* context, PlaybackContext* playback) const override {
    context->drawImage(image, sampling, playback->state(), playback->brush());
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

  PictureRecordType type() const override {
    return PictureRecordType::DrawImageRect;
  }

  void playback(DrawContext* context, PlaybackContext* playback) const override {
    context->drawImageRect(image, rect, rect, sampling, playback->state(), playback->brush(),
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

  PictureRecordType type() const override {
    return PictureRecordType::DrawImageRectToRect;
  }

  void playback(DrawContext* context, PlaybackContext* playback) const override {
    context->drawImageRect(image, rect, dstRect, sampling, playback->state(), playback->brush(),
                           constraint);
  }

  Rect dstRect;
};

class DrawGlyphRunList : public PictureRecord {
 public:
  explicit DrawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList)
      : glyphRunList(std::move(glyphRunList)) {
  }

  PictureRecordType type() const override {
    return PictureRecordType::DrawGlyphRunList;
  }

  void playback(DrawContext* context, PlaybackContext* playback) const override {
    context->drawGlyphRunList(glyphRunList, playback->state(), playback->brush(),
                              playback->stroke());
  }

  std::shared_ptr<GlyphRunList> glyphRunList;
};

class DrawPicture : public PictureRecord {
 public:
  explicit DrawPicture(std::shared_ptr<Picture> picture) : picture(std::move(picture)) {
  }

  PictureRecordType type() const override {
    return PictureRecordType::DrawPicture;
  }

  bool hasUnboundedFill(bool& hasInverseClip) const override {
    return hasInverseClip && picture->hasUnboundedFill();
  }

  void playback(DrawContext* context, PlaybackContext* playback) const override {
    context->drawPicture(picture, playback->state());
  }

  std::shared_ptr<Picture> picture;
};

class DrawLayer : public PictureRecord {
 public:
  DrawLayer(std::shared_ptr<Picture> picture, std::shared_ptr<ImageFilter> filter)
      : picture(std::move(picture)), filter(std::move(filter)) {
  }

  PictureRecordType type() const override {
    return PictureRecordType::DrawLayer;
  }

  bool hasUnboundedFill(bool& hasInverseClip) const override {
    return hasInverseClip && picture->hasUnboundedFill();
  }

  void playback(DrawContext* context, PlaybackContext* playback) const override {
    context->drawLayer(picture, filter, playback->state(), playback->brush());
  }

  std::shared_ptr<Picture> picture;
  std::shared_ptr<ImageFilter> filter;
};
}  // namespace tgfx
