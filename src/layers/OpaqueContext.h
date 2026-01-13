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
class OpaqueContext : public DrawContext {
 public:
  OpaqueContext();

  ~OpaqueContext() override = default;

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
  struct OpaqueShape {
    enum class Type { None, Fill, Rect, RRect, Path, Shape };

    OpaqueShape() : shape(nullptr) {
    }
    explicit OpaqueShape(Type t) : shape(nullptr), type(t) {
      DEBUG_ASSERT(t == Type::None || t == Type::Fill);
    }
    OpaqueShape(const Rect& r, const Stroke* s) : rect(r), type(Type::Rect) {
      if (s) {
        stroke = *s;
        hasStroke = true;
      }
    }
    OpaqueShape(const RRect& rr, const Stroke* s) : rRect(rr), type(Type::RRect) {
      if (s) {
        stroke = *s;
        hasStroke = true;
      }
    }
    explicit OpaqueShape(const Path& p) : path(p), type(Type::Path) {
    }
    OpaqueShape(std::shared_ptr<Shape> s, const Stroke* stk)
        : shape(std::move(s)), type(Type::Shape) {
      if (stk) {
        stroke = *stk;
        hasStroke = true;
      }
    }

    OpaqueShape(const OpaqueShape& other) {
      *this = other;
    }

    ~OpaqueShape() {
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
    bool operator==(const OpaqueShape& other) const;
    bool operator!=(const OpaqueShape& other) const {
      return !(*this == other);
    }
    OpaqueShape& operator=(const OpaqueShape& other);
  };

  void drawOpaqueShape(const OpaqueShape& opaqueShape, const MCState& state, const Brush& brush);

  bool containOpaqueBound(const Rect& bounds);

  void mergeOpaqueBound(const Rect& bounds);

  bool canAppend(const OpaqueShape& opaqueShape, const MCState& state, const Brush& brush);

  void appendFill(const Brush& brush);

  void flushPendingShape(const OpaqueShape& opaqueShape = {}, const MCState& state = {},
                         const Brush& brush = {});

  void resetPendingShape(const OpaqueShape& opaqueShape, const MCState& state, const Brush& brush);
  OpaqueShape pendingShape = {};

  MCState pendingState = {};
  std::vector<Brush> pendingBrushes = {};

  std::vector<Rect> opaqueBounds = {};
  PictureContext pictureContext = {};

  friend class PendingShapeAutoReset;
};
}  // namespace tgfx
