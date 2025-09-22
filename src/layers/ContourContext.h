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
#include "core/RecordingContext.h"

namespace tgfx {
class ContourContext : public DrawContext {
 public:
  ContourContext();

  ~ContourContext() override = default;

  void drawFill(const Fill& fill) override;

  void drawRect(const Rect& rect, const MCState& state, const Fill& fill) override;

  void drawRRect(const RRect& rRect, const MCState& state, const Fill& fill,
                 const Stroke* stroke) override;

  void drawPath(const Path& path, const MCState& state, const Fill& fill) override;

  void drawShape(std::shared_ptr<Shape> shape, const MCState& state, const Fill& fill,
                 const Stroke* stroke) override;

  void drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                 const MCState& state, const Fill& fill) override;

  void drawImageRect(std::shared_ptr<Image> image, const Rect& srcRect, const Rect& dstRect,
                     const SamplingOptions& sampling, const MCState& state, const Fill& fill,
                     SrcRectConstraint constraint) override;

  void drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList, const MCState& state,
                        const Fill& fill, const Stroke* stroke) override;

  void drawPicture(std::shared_ptr<Picture> picture, const MCState& state) override;

  void drawLayer(std::shared_ptr<Picture> picture, std::shared_ptr<ImageFilter> filter,
                 const MCState& state, const Fill& fill) override;

  std::shared_ptr<Picture> finishRecordingAsPicture();

 private:
  struct Contour {
    enum class Type { None, Fill, Rect, RRect, Path, Shape };

    Contour() : shape(nullptr) {
    }
    explicit Contour(Type t) : shape(nullptr), type(t) {
      DEBUG_ASSERT(t == Type::None || t == Type::Fill);
    }
    explicit Contour(const Rect& r) : rect(r), type(Type::Rect) {
    }
    explicit Contour(const RRect& rr) : rRect(rr), type(Type::RRect) {
    }
    explicit Contour(const Path& p) : path(p), type(Type::Path) {
    }
    explicit Contour(std::shared_ptr<Shape> s) : shape(std::move(s)), type(Type::Shape) {
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
    bool isInverseFillType() const;
    Rect getBounds() const;
    void draw(RecordingContext& context, const MCState& state, const Fill& fill,
              const Stroke* stroke) const;
    bool operator==(const Contour& other) const;
    bool operator!=(const Contour& other) const {
      return !(*this == other);
    }
    Contour& operator=(const Contour& other);
  };

  void drawContour(const Contour& contour, const MCState& state, const Fill& fill,
                   const Stroke* stroke = nullptr);

  bool containContourBound(const Rect& bounds);

  void mergeContourBound(const Rect& bounds);

  bool canAppend(const Contour& contour, const MCState& state, const Fill& fill,
                 const Stroke* stroke = nullptr);

  void appendFill(const Fill& fill, const Stroke* stroke = nullptr);

  void flushPendingContour(const Contour& contour = {}, const MCState& state = {},
                           const Fill& fill = {}, const Stroke* stroke = nullptr);

  void resetPendingContour(const Contour& contour, const MCState& state, const Fill& fill,
                           const Stroke* stroke);
  Contour pendingContour = {};

  MCState pendingState = {};
  std::vector<Fill> pendingFills = {};
  std::vector<const Stroke*> pendingStrokes = {};

  std::vector<Rect> contourBounds = {};
  RecordingContext recordingContext = {};

  friend class PendingContourAutoReset;
};
}  // namespace tgfx
