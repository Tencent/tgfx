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
  DrawFill,
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

class DrawFill : public Record {
 public:
  DrawFill(MCState state, Fill fill) : Record(std::move(state)), fill(std::move(fill)) {
  }

  RecordType type() const override {
    return RecordType::DrawFill;
  }

  void playback(DrawContext* context) const override {
    context->drawFill(state, fill);
  }

  Fill fill;
};

class DrawRect : public Record {
 public:
  DrawRect(const Rect& rect, MCState state, Fill fill)
      : Record(std::move(state)), fill(std::move(fill)), rect(rect) {
  }

  RecordType type() const override {
    return RecordType::DrawRect;
  }

  void playback(DrawContext* context) const override {
    context->drawRect(rect, state, fill);
  }

  Fill fill;
  Rect rect;
};

class DrawRRect : public Record {
 public:
  DrawRRect(const RRect& rRect, MCState state, Fill fill)
      : Record(std::move(state)), fill(std::move(fill)), rRect(rRect) {
  }

  RecordType type() const override {
    return RecordType::DrawRRect;
  }

  void playback(DrawContext* context) const override {
    context->drawRRect(rRect, state, fill);
  }

  Fill fill;
  RRect rRect;
};

class DrawShape : public Record {
 public:
  DrawShape(std::shared_ptr<Shape> shape, MCState state, Fill fill)
      : Record(std::move(state)), fill(std::move(fill)), shape(std::move(shape)) {
  }

  RecordType type() const override {
    return RecordType::DrawShape;
  }

  void playback(DrawContext* context) const override {
    context->drawShape(shape, state, fill);
  }

  Fill fill;
  std::shared_ptr<Shape> shape;
};

class DrawImage : public Record {
 public:
  DrawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling, MCState state, Fill fill)
      : Record(std::move(state)), fill(std::move(fill)), image(std::move(image)),
        sampling(sampling) {
  }

  RecordType type() const override {
    return RecordType::DrawImage;
  }

  void playback(DrawContext* context) const override {
    context->drawImage(image, sampling, state, fill);
  }

  Fill fill;
  std::shared_ptr<Image> image;
  SamplingOptions sampling;
};

class DrawImageRect : public DrawImage {
 public:
  DrawImageRect(std::shared_ptr<Image> image, const Rect& rect, const SamplingOptions& sampling,
                MCState state, Fill fill)
      : DrawImage(std::move(image), sampling, std::move(state), std::move(fill)), rect(rect) {
  }

  RecordType type() const override {
    return RecordType::DrawImageRect;
  }

  void playback(DrawContext* context) const override {
    context->drawImageRect(image, rect, sampling, state, fill);
  }

  Rect rect;
};

class DrawGlyphRunList : public Record {
 public:
  DrawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList, MCState state, Fill fill)
      : Record(std::move(state)), fill(std::move(fill)), glyphRunList(std::move(glyphRunList)) {
  }

  RecordType type() const override {
    return RecordType::DrawGlyphRunList;
  }

  void playback(DrawContext* context) const override {
    context->drawGlyphRunList(glyphRunList, state, fill, nullptr);
  }

  Fill fill;
  std::shared_ptr<GlyphRunList> glyphRunList;
};

class StrokeGlyphRunList : public DrawGlyphRunList {
 public:
  StrokeGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList, MCState state, Fill fill,
                     const Stroke& stroke)
      : DrawGlyphRunList(std::move(glyphRunList), std::move(state), std::move(fill)),
        stroke(stroke) {
  }

  RecordType type() const override {
    return RecordType::StrokeGlyphRunList;
  }

  void playback(DrawContext* context) const override {
    context->drawGlyphRunList(glyphRunList, state, fill, &stroke);
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
            Fill fill)
      : Record(std::move(state)), fill(std::move(fill)), picture(std::move(picture)),
        filter(std::move(filter)) {
  }

  RecordType type() const override {
    return RecordType::DrawLayer;
  }

  void playback(DrawContext* context) const override {
    context->drawLayer(picture, filter, state, fill);
  }

  Fill fill;
  std::shared_ptr<Picture> picture;
  std::shared_ptr<ImageFilter> filter;
};
}  // namespace tgfx
