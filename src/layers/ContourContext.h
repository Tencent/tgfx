/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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
#include "core/PictureContext.h"
#include "tgfx/core/Brush.h"

namespace tgfx {
class ContourContext : public DrawContext {
 public:
  ContourContext();

  ~ContourContext() override = default;

  void drawFill(const Brush& brush) override;

  void drawRect(const Rect& rect, const MCState& state, const Brush& brush,
                const Stroke* stroke) override;

  void drawRRect(const RRect& rRect, const MCState& state, const Brush& brush,
                 const Stroke* stroke) override;

  void drawPath(const Path& path, const MCState& state, const Brush& brush) override;

  void drawShape(std::shared_ptr<Shape> shape, const MCState& state, const Brush& brush,
                 const Stroke* stroke) override;

  void drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                 const MCState& state, const Brush& brush) override;

  void drawImageRect(std::shared_ptr<Image> image, const Rect& srcRect, const Rect& dstRect,
                     const SamplingOptions& sampling, const MCState& state, const Brush& brush,
                     SrcRectConstraint constraint) override;

  void drawTextBlob(std::shared_ptr<TextBlob> textBlob, const MCState& state, const Brush& brush,
                    const Stroke* stroke) override;

  void drawPicture(std::shared_ptr<Picture> picture, const MCState& state) override;

  void drawLayer(std::shared_ptr<Picture> picture, std::shared_ptr<ImageFilter> filter,
                 const MCState& state, const Brush& brush) override;

  std::shared_ptr<Picture> finishRecordingAsPicture();

 private:
  struct Contour {
    enum class Type { None, Fill, Rect, RRect, Path, Shape };

    Contour() : shape(nullptr) {
    }
    explicit Contour(Type t) : shape(nullptr), type(t) {
      DEBUG_ASSERT(t == Type::None || t == Type::Fill);
    }
    Contour(const Rect& r, const Stroke* s) : rect(r), type(Type::Rect) {
      if (s) {
        stroke = *s;
        hasStroke = true;
      }
    }
    Contour(const RRect& rr, const Stroke* s) : rRect(rr), type(Type::RRect) {
      if (s) {
        stroke = *s;
        hasStroke = true;
      }
    }
    explicit Contour(const Path& p) : path(p), type(Type::Path) {
    }
    Contour(std::shared_ptr<Shape> s, const Stroke* stk) : shape(std::move(s)), type(Type::Shape) {
      if (stk) {
        stroke = *stk;
        hasStroke = true;
      }
    }

    Contour(const Contour& other) {
      *this = other;
    }

    ~Contour() {
      if (type == Type::Path) {
        path.~Path();
      } else if (type == Type::Shape) {
        shape.~shared_ptr<Shape>();
      }
    }

    union {
      Rect rect;
      RRect rRect;
      Path path;
      std::shared_ptr<Shape> shape;
    };

    Type type = Type::None;
    Stroke stroke = {};
    bool hasStroke = false;

    bool isInverseFillType() const;
    Rect getBounds() const;
    void draw(PictureContext& context, const MCState& state, const Brush& brush) const;
    bool operator==(const Contour& other) const;
    bool operator!=(const Contour& other) const {
      return !(*this == other);
    }
    Contour& operator=(const Contour& other);
  };

  void drawContour(const Contour& contour, const MCState& state, const Brush& brush);

  bool containContourBound(const Rect& bounds);

  void mergeContourBound(const Rect& bounds);

  bool canAppend(const Contour& contour, const MCState& state, const Brush& brush);

  void appendFill(const Brush& brush);

  void flushPendingContour(const Contour& contour = {}, const MCState& state = {},
                           const Brush& brush = {});

  void resetPendingContour(const Contour& contour, const MCState& state, const Brush& brush);
  Contour pendingContour = {};

  MCState pendingState = {};
  std::vector<Brush> pendingBrushes = {};

  std::vector<Rect> contourBounds = {};
  PictureContext pictureContext = {};

  friend class PendingContourAutoReset;
};
}  // namespace tgfx
