/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <optional>
#include <string>
#include "svg/SVGRenderContext.h"
#include "tgfx/core/Font.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Typeface.h"
#include "tgfx/svg/node/SVGText.h"
#include "tgfx/svg/shaper/Shaper.h"

namespace tgfx {

class SVGTextContext final : RunHandler {
 public:
  using ShapedTextCallback =
      std::function<void(const SVGRenderContext&, const std::shared_ptr<TextBlob>&)>;

  // Helper for encoding optional positional attributes.
  class PosAttrs {
   public:
    enum Attr : size_t {
      x = 0,
      y = 1,
      dx = 2,
      dy = 3,
      rotate = 4,
    };

    float operator[](Attr a) const {
      return storage[a];
    }
    float& operator[](Attr a) {
      return storage[a];
    }

    bool has(Attr a) const {
      return storage[a] != _NONE;
    }
    bool hasAny() const {
      return this->has(x) || this->has(y) || this->has(dx) || this->has(dy) || this->has(rotate);
    }

    void setImplicitRotate(bool imp) {
      implicitRotate = imp;
    }
    bool isImplicitRotate() const {
      return implicitRotate;
    }

   private:
    inline static constexpr auto _NONE = std::numeric_limits<float>::infinity();
    float storage[5] = {_NONE, _NONE, _NONE, _NONE, _NONE};
    bool implicitRotate = false;
  };

  // Helper for cascading position attribute resolution (x, y, dx, dy, rotate) [1]:
  //   - each text position element can specify an arbitrary-length attribute array
  //   - for each character, we look up a given attribute first in its local attribute array,
  //     then in the ancestor chain (cascading/fallback) - and return the first value encountered.
  //   - the lookup is based on character index relative to the text content subtree
  //     (i.e. the index crosses chunk boundaries)
  //
  // [1] https://www.w3.org/TR/SVG11/text.html#TSpanElementXAttribute
  class ScopedPosResolver {
   public:
    ScopedPosResolver(const SVGTextContainer&, const SVGLengthContext&, SVGTextContext*, size_t);

    ScopedPosResolver(const SVGTextContainer&, const SVGLengthContext&, SVGTextContext*);

    ~ScopedPosResolver();

    PosAttrs resolve(size_t charIndex) const;

   private:
    SVGTextContext* textContext;
    const ScopedPosResolver* parent;  // parent resolver (fallback)
    const size_t charIndexOffset;     // start index for the current resolver
    const std::vector<float> x, y, dX, dY;
    const std::vector<float>& rotate;

    // cache for the last known index with explicit positioning
    mutable size_t fLastPosIndex = std::numeric_limits<size_t>::max();
  };

  SVGTextContext(const SVGRenderContext&, const ShapedTextCallback&, const SVGTextPath* = nullptr);
  ~SVGTextContext() override;

  // Shape and queue codepoints for final alignment.
  void shapeFragment(const std::string&, const SVGRenderContext&, SVGXmlSpace);

  // Perform final adjustments and push shaped blobs to the callback.
  void flushChunk(const SVGRenderContext& ctx);

  const ShapedTextCallback& getCallback() const {
    return callback;
  }

 private:
  struct PositionAdjustment {
    Point offset;
    float rotation;
  };

  struct ShapeBuffer {
    std::string utf8;
    // per-utf8-char cumulative pos adjustments
    std::vector<PositionAdjustment> utf8PosAdjust;

    void reserve(size_t size) {
      utf8.reserve(size);
      utf8PosAdjust.reserve(utf8PosAdjust.size());
    }

    void reset() {
      utf8.clear();
      utf8PosAdjust.clear();
    }

    void append(Unichar, PositionAdjustment);
  };

  struct RunRec {
    Font font;
    std::unique_ptr<Paint> fillPaint, strokePaint;
    std::unique_ptr<GlyphID[]> glyphs;                     // filled by SkShaper
    std::unique_ptr<Point[]> glyphPos;                     // filled by SkShaper
    std::unique_ptr<PositionAdjustment[]> glyphPosAdjust;  // deferred positioning adjustments
    size_t glyphCount;
    Point advance;
  };

  // Caches path information to accelerate position lookups.
  class PathData {
   public:
    PathData(const SVGRenderContext&, const SVGTextPath&);

    Matrix getMatrixAt(float offset) const;

    float length() const {
      return fLength;
    }

   private:
    // std::vector<sk_sp<SkContourMeasure>> fContours;
    float fLength = 0;  // total path length
  };

  void shapePendingBuffer(const SVGRenderContext&, const Font&);

  Matrix computeGlyphXform(GlyphID, const Font&, const Point& glyph_pos,
                           const PositionAdjustment&) const;

  //callbacks
  void beginLine() override {
  }

  void runInfo(const RunInfo&) override {
  }

  void commitRunInfo() override {
  }

  Buffer runBuffer(const RunInfo& ri) override;

  void commitRunBuffer(const RunInfo& ri) override;

  void commitLine() override;

  // http://www.w3.org/TR/SVG11/text.html#TextLayout
  const SVGRenderContext& originalContext;  // original render context
  const ShapedTextCallback& callback;
  std::unique_ptr<Shaper> shaper = nullptr;
  std::vector<RunRec> runs = {};
  const ScopedPosResolver* posResolver = nullptr;

  // shaper state
  ShapeBuffer shapeBuffer = {};
  std::vector<uint32_t> shapeClusterBuffer = {};

  // chunk state
  Point chunkPos = {0, 0};      // current text chunk position
  Point chunkAdvance = {0, 0};  // cumulative advance
  float chunkAlignmentFactor;   // current chunk alignment

  // tracks the global text subtree char index (cross chunks).  Used for position resolution.
  size_t currentCharIndex = 0;

  // cached for access from SkShaper callbacks.
  std::optional<Paint> currentFill;
  std::optional<Paint> currentStroke;

  bool prevCharSpace = true;  // WS filter state
  bool forcePrimitiveShaping = false;
};

}  // namespace tgfx