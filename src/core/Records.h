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
  DrawRect,
  DrawRRect,
  DrawPath,
  StrokePath,
  DrawImage,
  DrawImageRect,
  DrawGlyphRunList,
  StrokeGlyphRunList,
  DrawPicture,
  DrawLayer
};

class Record {
 public:
  virtual ~Record() = default;

  virtual RecordType type() const = 0;

  virtual void playback(DrawContext* context) const = 0;
};

class DrawRect : public Record {
 public:
  DrawRect(const Rect& rect, MCState state, FillStyle style)
      : rect(rect), state(std::move(state)), style(std::move(style)) {
  }

  RecordType type() const override {
    return RecordType::DrawRect;
  }

  void playback(DrawContext* context) const override {
    context->drawRect(rect, state, style);
  }

  Rect rect;
  MCState state;
  FillStyle style;
};

class DrawRRect : public Record {
 public:
  DrawRRect(const RRect& rRect, MCState state, FillStyle style)
      : rRect(rRect), state(std::move(state)), style(std::move(style)) {
  }

  RecordType type() const override {
    return RecordType::DrawRRect;
  }

  void playback(DrawContext* context) const override {
    context->drawRRect(rRect, state, style);
  }

  RRect rRect;
  MCState state;
  FillStyle style;
};

class DrawPath : public Record {
 public:
  DrawPath(Path path, MCState state, FillStyle style)
      : path(std::move(path)), state(std::move(state)), style(std::move(style)) {
  }

  RecordType type() const override {
    return RecordType::DrawPath;
  }

  void playback(DrawContext* context) const override {
    context->drawPath(path, state, style, nullptr);
  }

 protected:
  Path path;
  MCState state;
  FillStyle style;
};

class StrokePath : public DrawPath {
 public:
  StrokePath(Path path, MCState state, FillStyle style, const Stroke& stroke)
      : DrawPath(std::move(path), std::move(state), std::move(style)), stroke(stroke) {
  }

  RecordType type() const override {
    return RecordType::StrokePath;
  }

  void playback(DrawContext* context) const override {
    context->drawPath(path, state, style, &stroke);
  }

  Stroke stroke;
};

class DrawImage : public Record {
 public:
  DrawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling, MCState state,
            FillStyle style)
      : image(std::move(image)), sampling(sampling), state(std::move(state)),
        style(std::move(style)) {
  }

  RecordType type() const override {
    return RecordType::DrawImage;
  }

  void playback(DrawContext* context) const override {
    context->drawImageRect(image, sampling, Rect::MakeWH(image->width(), image->height()), state,
                           style);
  }

  std::shared_ptr<Image> image;
  SamplingOptions sampling;
  MCState state;
  FillStyle style;
};

class DrawImageRect : public DrawImage {
 public:
  DrawImageRect(std::shared_ptr<Image> image, const SamplingOptions& sampling, const Rect& rect,
                MCState state, FillStyle style)
      : DrawImage(std::move(image), sampling, std::move(state), std::move(style)), rect(rect) {
  }

  RecordType type() const override {
    return RecordType::DrawImageRect;
  }

  void playback(DrawContext* context) const override {
    context->drawImageRect(image, sampling, rect, state, style);
  }

  Rect rect;
};

class DrawGlyphRunList : public Record {
 public:
  DrawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList, MCState state, FillStyle style)
      : glyphRunList(std::move(glyphRunList)), state(std::move(state)), style(std::move(style)) {
  }

  RecordType type() const override {
    return RecordType::DrawGlyphRunList;
  }

  void playback(DrawContext* context) const override {
    context->drawGlyphRunList(glyphRunList, state, style, nullptr);
  }

  std::shared_ptr<GlyphRunList> glyphRunList;
  MCState state;
  FillStyle style;
};

class StrokeGlyphRunList : public DrawGlyphRunList {
 public:
  StrokeGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList, MCState state, FillStyle style,
                     const Stroke& stroke)
      : DrawGlyphRunList(std::move(glyphRunList), std::move(state), std::move(style)),
        stroke(stroke) {
  }

  RecordType type() const override {
    return RecordType::StrokeGlyphRunList;
  }

  void playback(DrawContext* context) const override {
    context->drawGlyphRunList(glyphRunList, state, style, &stroke);
  }

  Stroke stroke;
};

class DrawPicture : public Record {
 public:
  DrawPicture(std::shared_ptr<Picture> picture, MCState state)
      : picture(std::move(picture)), state(std::move(state)) {
  }

  RecordType type() const override {
    return RecordType::DrawPicture;
  }

  void playback(DrawContext* context) const override {
    context->drawPicture(picture, state);
  }

  std::shared_ptr<Picture> picture;
  MCState state;
};

class DrawLayer : public Record {
 public:
  DrawLayer(std::shared_ptr<Picture> picture, MCState state, FillStyle style,
            std::shared_ptr<ImageFilter> filter)
      : picture(std::move(picture)), state(std::move(state)), style(std::move(style)),
        filter(std::move(filter)) {
  }

  RecordType type() const override {
    return RecordType::DrawLayer;
  }

  void playback(DrawContext* context) const override {
    context->drawLayer(picture, state, style, filter);
  }

  std::shared_ptr<Picture> picture;
  MCState state;
  FillStyle style;
  std::shared_ptr<ImageFilter> filter;
};
}  // namespace tgfx
