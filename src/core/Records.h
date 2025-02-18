/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

namespace tgfx {
enum class RecordType {
  DrawStyle,
  DrawRect,
  DrawRRect,
  DrawShape,
  DrawImage,
  DrawImageRect,
  DrawGlyphRunList,
  StrokeGlyphRunList,
  DrawPicture,
  DrawLayer
};

class Record {
 public:
  explicit Record(MCState state) : state(std::move(state)) {
  }

  virtual ~Record() = default;

  virtual RecordType type() const = 0;

  virtual void playback(DrawContext* context) const = 0;

  MCState state;
};

class DrawStyle : public Record {
 public:
  DrawStyle(MCState state, FillStyle style) : Record(std::move(state)), style(std::move(style)) {
  }

  RecordType type() const override {
    return RecordType::DrawStyle;
  }

  void playback(DrawContext* context) const override {
    context->drawStyle(state, style);
  }

  FillStyle style;
};

class DrawRect : public Record {
 public:
  DrawRect(const Rect& rect, MCState state, FillStyle style)
      : Record(std::move(state)), style(std::move(style)), rect(rect) {
  }

  RecordType type() const override {
    return RecordType::DrawRect;
  }

  void playback(DrawContext* context) const override {
    context->drawRect(rect, state, style);
  }

  FillStyle style;
  Rect rect;
};

class DrawRRect : public Record {
 public:
  DrawRRect(const RRect& rRect, MCState state, FillStyle style)
      : Record(std::move(state)), style(std::move(style)), rRect(rRect) {
  }

  RecordType type() const override {
    return RecordType::DrawRRect;
  }

  void playback(DrawContext* context) const override {
    context->drawRRect(rRect, state, style);
  }

  FillStyle style;
  RRect rRect;
};

class DrawShape : public Record {
 public:
  DrawShape(std::shared_ptr<Shape> shape, MCState state, FillStyle style)
      : Record(std::move(state)), style(std::move(style)), shape(std::move(shape)) {
  }

  RecordType type() const override {
    return RecordType::DrawShape;
  }

  void playback(DrawContext* context) const override {
    context->drawShape(shape, state, style);
  }

  FillStyle style;
  std::shared_ptr<Shape> shape;
};

class DrawImage : public Record {
 public:
  DrawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling, MCState state,
            FillStyle style)
      : Record(std::move(state)), style(std::move(style)), image(std::move(image)),
        sampling(sampling) {
  }

  RecordType type() const override {
    return RecordType::DrawImage;
  }

  void playback(DrawContext* context) const override {
    context->drawImage(image, sampling, state, style);
  }

  FillStyle style;
  std::shared_ptr<Image> image;
  SamplingOptions sampling;
};

class DrawImageRect : public DrawImage {
 public:
  DrawImageRect(std::shared_ptr<Image> image, const Rect& rect, const SamplingOptions& sampling,
                MCState state, FillStyle style)
      : DrawImage(std::move(image), sampling, std::move(state), std::move(style)), rect(rect) {
  }

  RecordType type() const override {
    return RecordType::DrawImageRect;
  }

  void playback(DrawContext* context) const override {
    context->drawImageRect(image, rect, sampling, state, style);
  }

  Rect rect;
};

class DrawGlyphRunList : public Record {
 public:
  DrawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList, MCState state, FillStyle style)
      : Record(std::move(state)), style(std::move(style)), glyphRunList(std::move(glyphRunList)) {
  }

  RecordType type() const override {
    return RecordType::DrawGlyphRunList;
  }

  void playback(DrawContext* context) const override {
    context->drawGlyphRunList(glyphRunList, nullptr, state, style);
  }

  FillStyle style;
  std::shared_ptr<GlyphRunList> glyphRunList;
};

class StrokeGlyphRunList : public DrawGlyphRunList {
 public:
  StrokeGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList, const Stroke& stroke,
                     MCState state, FillStyle style)
      : DrawGlyphRunList(std::move(glyphRunList), std::move(state), std::move(style)),
        stroke(stroke) {
  }

  RecordType type() const override {
    return RecordType::StrokeGlyphRunList;
  }

  void playback(DrawContext* context) const override {
    context->drawGlyphRunList(glyphRunList, &stroke, state, style);
  }

  Stroke stroke;
};

class DrawPicture : public Record {
 public:
  DrawPicture(std::shared_ptr<Picture> picture, MCState state)
      : Record(std::move(state)), picture(std::move(picture)) {
  }

  RecordType type() const override {
    return RecordType::DrawPicture;
  }

  void playback(DrawContext* context) const override {
    context->drawPicture(picture, state);
  }

  std::shared_ptr<Picture> picture;
};

class DrawLayer : public Record {
 public:
  DrawLayer(std::shared_ptr<Picture> picture, std::shared_ptr<ImageFilter> filter, MCState state,
            FillStyle style)
      : Record(std::move(state)), style(std::move(style)), picture(std::move(picture)),
        filter(std::move(filter)) {
  }

  RecordType type() const override {
    return RecordType::DrawLayer;
  }

  void playback(DrawContext* context) const override {
    context->drawLayer(picture, filter, state, style);
  }

  FillStyle style;
  std::shared_ptr<Picture> picture;
  std::shared_ptr<ImageFilter> filter;
};
}  // namespace tgfx
