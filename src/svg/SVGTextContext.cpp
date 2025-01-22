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

#include "SVGTextContext.h"
#include <cfloat>
#include <climits>
#include <string>
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "tgfx/core/Font.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Typeface.h"
#include "tgfx/core/UTF.h"
#include "tgfx/svg/shaper/ShaperFactory.h"

namespace tgfx {

namespace {

std::vector<float> ResolveLengths(const SVGLengthContext& lctx,
                                  const std::vector<SVGLength>& lengths,
                                  SVGLengthContext::LengthType lt) {
  std::vector<float> resolved;
  resolved.reserve(lengths.size());

  for (const auto& l : lengths) {
    resolved.push_back(lctx.resolve(l, lt));
  }

  return resolved;
}

}  // namespace

SVGTextContext::ScopedPosResolver::ScopedPosResolver(const SVGTextContainer& txt,
                                                     const SVGLengthContext& lctx,
                                                     SVGTextContext* tctx, size_t charIndexOffset)
    : textContext(tctx), parent(tctx->posResolver), charIndexOffset(charIndexOffset),
      x(ResolveLengths(lctx, txt.getX(), SVGLengthContext::LengthType::Horizontal)),
      y(ResolveLengths(lctx, txt.getY(), SVGLengthContext::LengthType::Vertical)),
      dX(ResolveLengths(lctx, txt.getDx(), SVGLengthContext::LengthType::Horizontal)),
      dY(ResolveLengths(lctx, txt.getDy(), SVGLengthContext::LengthType::Vertical)),
      rotate(txt.getRotate()) {
  textContext->posResolver = this;
}

SVGTextContext::ScopedPosResolver::ScopedPosResolver(const SVGTextContainer& txt,
                                                     const SVGLengthContext& lctx,
                                                     SVGTextContext* tctx)
    : ScopedPosResolver(txt, lctx, tctx, tctx->currentCharIndex) {
}

SVGTextContext::ScopedPosResolver::~ScopedPosResolver() {
  textContext->posResolver = parent;
}

///////////////////////////////////////////////////////////////////////////////

SVGTextContext::PosAttrs SVGTextContext::ScopedPosResolver::resolve(size_t charIndex) const {
  PosAttrs attrs;

  if (charIndex < fLastPosIndex) {
    ASSERT(charIndex >= charIndexOffset);
    const auto localCharIndex = charIndex - charIndexOffset;

    const auto hasAllLocal = localCharIndex < x.size() && localCharIndex < y.size() &&
                             localCharIndex < dX.size() && localCharIndex < dY.size() &&
                             localCharIndex < rotate.size();
    if (!hasAllLocal && parent) {
      attrs = parent->resolve(charIndex);
    }

    if (localCharIndex < x.size()) {
      attrs[PosAttrs::x] = x[localCharIndex];
    }
    if (localCharIndex < y.size()) {
      attrs[PosAttrs::y] = y[localCharIndex];
    }
    if (localCharIndex < dX.size()) {
      attrs[PosAttrs::dx] = dX[localCharIndex];
    }
    if (localCharIndex < dY.size()) {
      attrs[PosAttrs::dy] = dY[localCharIndex];
    }

    // Rotation semantics are interestingly different [1]:
    //
    //   - values are not cumulative
    //   - if explicit values are present at any level in the ancestor chain, those take
    //     precedence (closest ancestor)
    //   - last specified value applies to all remaining chars (closest ancestor)
    //   - these rules apply at node scope (not chunk scope)
    //
    // This means we need to discriminate between explicit rotation (rotate value provided for
    // current char) and implicit rotation (ancestor has some values - but not for the requested
    // char - we use the last specified value).
    //
    // [1] https://www.w3.org/TR/SVG11/text.html#TSpanElementRotateAttribute
    if (!rotate.empty()) {
      if (localCharIndex < rotate.size()) {
        // Explicit rotation value overrides anything in the ancestor chain.
        attrs[PosAttrs::rotate] = rotate[localCharIndex];
        attrs.setImplicitRotate(false);
      } else if (!attrs.has(PosAttrs::rotate) || attrs.isImplicitRotate()) {
        // Local implicit rotation (last specified value) overrides ancestor implicit
        // rotation.
        attrs[PosAttrs::rotate] = rotate.back();
        attrs.setImplicitRotate(true);
      }
    }

    if (!attrs.hasAny()) {
      // Once we stop producing explicit position data, there is no reason to
      // continue trying for higher indices.  We can suppress future lookups.
      fLastPosIndex = charIndex;
    }
  }

  return attrs;
}

void SVGTextContext::ShapeBuffer::append(Unichar ch, PositionAdjustment pos) {
  // relative pos adjustments are cumulative
  if (!utf8PosAdjust.empty()) {
    pos.offset += utf8PosAdjust.back().offset;
  }

  auto unf8String = UTF::ToUTF8(ch);
  utf8.append(unf8String);
  utf8PosAdjust.insert(utf8PosAdjust.end(), unf8String.size(), pos);
}

void SVGTextContext::shapePendingBuffer(const SVGRenderContext& ctx, const Font& font) {
  const char* utf8 = shapeBuffer.utf8.data();
  size_t utf8Bytes = shapeBuffer.utf8.size();

  std::unique_ptr<FontRunIterator> font_runs =
      Shaper::MakeFontMgrRunIterator(utf8, utf8Bytes, font, ctx.fontManager());
  if (!font_runs) {
    return;
  }

  // Try to use the passed in shaping callbacks to shape, for example, using harfbuzz and ICU.
  const uint8_t defaultLTR = 0;
  std::unique_ptr<BiDiRunIterator> bidi = ctx.makeBidiRunIterator(utf8, utf8Bytes, defaultLTR);
  std::unique_ptr<LanguageRunIterator> language =
      Shaper::MakeStdLanguageRunIterator(utf8, utf8Bytes);
  std::unique_ptr<ScriptRunIterator> script = ctx.makeScriptRunIterator(utf8, utf8Bytes);

  if (bidi && script && language) {
    shaper->shape(utf8, utf8Bytes, *font_runs, *bidi, *script, *language, nullptr, 0, FLT_MAX,
                  this);
    shapeBuffer.reset();
    return;
  }  // If any of the callbacks fail, we'll fallback to the primitive shaping.
}

SVGTextContext::SVGTextContext(const SVGRenderContext& ctx, const ShapedTextCallback& cb,
                               const SVGTextPath* tpath)
    : originalContext(ctx), callback(cb), shaper(ctx.makeShaper()),
      chunkAlignmentFactor(ctx.presentationContext()._inherited.TextAnchor->getAlignmentFactor()) {

  if (tpath) {
    chunkPos.x = tpath->getStartOffset().value();
  }
}

SVGTextContext::~SVGTextContext() {
  this->flushChunk(originalContext);
}

void SVGTextContext::shapeFragment(const std::string& txt, const SVGRenderContext& ctx,
                                   SVGXmlSpace xs) {
  // https://www.w3.org/TR/SVG11/text.html#WhiteSpace
  // https://www.w3.org/TR/2008/REC-xml-20081126/#NT-S
  auto filterWSDefault = [this](Unichar ch) -> Unichar {
    // Remove all newline chars.
    if (ch == '\n') {
      return -1;
    }

    // Convert tab chars to space.
    if (ch == '\t') {
      ch = ' ';
    }

    // Consolidate contiguous space chars and strip leading spaces (fPrevCharSpace
    // starts off as true).
    if (prevCharSpace && ch == ' ') {
      return -1;
    }
    return ch;
  };
  auto filterWSPreserve = [](Unichar ch) -> Unichar {
    // Convert newline and tab chars to space.
    if (ch == '\n' || ch == '\t') {
      ch = ' ';
    }
    return ch;
  };

  // Stash paints for access from SkShaper callbacks.
  currentFill = ctx.fillPaint();
  currentStroke = ctx.strokePaint();

  const auto font = ctx.resolveFont();
  shapeBuffer.reserve(txt.size());

  const char* chatPointer = txt.c_str();
  const char* charEnd = chatPointer + txt.size();

  while (chatPointer < charEnd) {
    auto ch = UTF::NextUTF8(&chatPointer, charEnd);
    ch = (xs == SVGXmlSpace::Default) ? filterWSDefault(ch) : filterWSPreserve(ch);

    if (ch < 0) {
      // invalid utf or char filtered out
      continue;
    }

    ASSERT(posResolver);
    const auto pos = posResolver->resolve(currentCharIndex++);

    // Absolute position adjustments define a new chunk.
    // (https://www.w3.org/TR/SVG11/text.html#TextLayoutIntroduction)
    if (pos.has(PosAttrs::x) || pos.has(PosAttrs::y)) {
      this->shapePendingBuffer(ctx, font);
      this->flushChunk(ctx);

      // New chunk position.
      if (pos.has(PosAttrs::x)) {
        chunkPos.x = pos[PosAttrs::x];
      }
      if (pos.has(PosAttrs::y)) {
        chunkPos.y = pos[PosAttrs::y];
      }
    }

    shapeBuffer.append(ch,
                       {
                           {
                               pos.has(PosAttrs::dx) ? pos[PosAttrs::dx] : 0,
                               pos.has(PosAttrs::dy) ? pos[PosAttrs::dy] : 0,
                           },
                           pos.has(PosAttrs::rotate) ? DegreesToRadians(pos[PosAttrs::rotate]) : 0,
                       });

    prevCharSpace = (ch == ' ');
  }

  this->shapePendingBuffer(ctx, font);

  // Note: at this point we have shaped and buffered RunRecs for the current fragment.
  // The active text chunk continues until an explicit or implicit flush.
}

Matrix SVGTextContext::computeGlyphXform(GlyphID /*glyph*/, const Font& /*font*/,
                                         const Point& glyph_pos,
                                         const PositionAdjustment& pos_adjust) const {
  Point pos = chunkPos + glyph_pos + pos_adjust.offset + chunkAdvance * chunkAlignmentFactor;
  return Matrix::MakeRotate(RadiansToDegrees(pos_adjust.rotation), pos.x, pos.y);
}

RunHandler::Buffer SVGTextContext::runBuffer(const RunInfo& ri) {
  ASSERT(ri.glyphCount);

  RunRec runRecord = {
      ri.font,
      currentFill.has_value() ? std::make_unique<Paint>(*currentFill) : nullptr,
      currentStroke.has_value() ? std::make_unique<Paint>(*currentStroke) : nullptr,
      std::make_unique<GlyphID[]>(ri.glyphCount),
      std::make_unique<Point[]>(ri.glyphCount),
      std::make_unique<PositionAdjustment[]>(ri.glyphCount),
      ri.glyphCount,
      ri.advance,
  };

  runs.push_back(std::move(runRecord));

  // Ensure sufficient space to temporarily fetch cluster information.
  shapeClusterBuffer.resize(std::max(shapeClusterBuffer.size(), ri.glyphCount));

  return {
      runs.back().glyphs.get(),
      runs.back().glyphPos.get(),
      nullptr,
      shapeClusterBuffer.data(),
      chunkAdvance,
  };
}

void SVGTextContext::commitRunBuffer(const RunInfo& ri) {
  const auto& current_run = runs.back();

  // stash position adjustments
  for (size_t i = 0; i < ri.glyphCount; ++i) {
    const auto utf8_index = shapeClusterBuffer[i];
    current_run.glyphPosAdjust[i] = shapeBuffer.utf8PosAdjust[utf8_index];
  }

  chunkAdvance += ri.advance;
}

void SVGTextContext::commitLine() {
  if (!shapeBuffer.utf8PosAdjust.empty()) {
    // Offset adjustments are cumulative - only advance the current chunk with the last value.
    chunkAdvance += shapeBuffer.utf8PosAdjust.back().offset;
  }
}

}  // namespace tgfx