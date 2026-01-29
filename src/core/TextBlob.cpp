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

#include "tgfx/core/TextBlob.h"
#include <cstdlib>
#include "core/GlyphTransform.h"
#include "core/RunRecord.h"
#include "core/ScalerContext.h"
#include "core/utils/AtomicCache.h"
#include "core/utils/FauxBoldScale.h"
#include "core/utils/MathExtra.h"
#include "core/utils/StrokeUtils.h"
#include "tgfx/core/TextBlobBuilder.h"
#include "tgfx/core/UTF.h"

namespace tgfx {

std::shared_ptr<TextBlob> TextBlob::MakeFrom(const std::string& text, const Font& font) {
  if (font.getTypeface() == nullptr) {
    return nullptr;
  }
  const char* textStart = text.data();
  const char* textStop = textStart + text.size();
  size_t glyphCount = 0;
  const char* ptr = textStart;
  while (ptr < textStop) {
    auto unichar = UTF::NextUTF8(&ptr, textStop);
    if (font.getGlyphID(unichar) > 0) {
      glyphCount++;
    }
  }
  if (glyphCount == 0) {
    return nullptr;
  }
  TextBlobBuilder builder;
  const auto& buffer = builder.allocRunPosH(font, glyphCount, 0.0f);
  auto emptyAdvance = font.getSize() / 2.0f;
  float xOffset = 0;
  size_t index = 0;
  ptr = textStart;
  while (ptr < textStop) {
    auto unichar = UTF::NextUTF8(&ptr, textStop);
    auto glyphID = font.getGlyphID(unichar);
    if (glyphID > 0) {
      buffer.glyphs[index] = glyphID;
      buffer.positions[index] = xOffset;
      xOffset += font.getAdvance(glyphID);
      index++;
    } else {
      xOffset += emptyAdvance;
    }
  }
  return builder.build();
}

std::shared_ptr<TextBlob> TextBlob::MakeFrom(const GlyphID glyphIDs[], const Point positions[],
                                             size_t glyphCount, const Font& font) {
  if (glyphCount == 0 || font.getTypeface() == nullptr) {
    return nullptr;
  }
  TextBlobBuilder builder;
  const auto& buffer = builder.allocRunPos(font, glyphCount);
  memcpy(buffer.glyphs, glyphIDs, glyphCount * sizeof(GlyphID));
  memcpy(buffer.positions, positions, glyphCount * 2 * sizeof(float));
  return builder.build();
}

std::shared_ptr<TextBlob> TextBlob::MakeFromPosH(const GlyphID glyphIDs[], const float xPositions[],
                                                 size_t glyphCount, float y, const Font& font) {
  if (glyphCount == 0 || font.getTypeface() == nullptr) {
    return nullptr;
  }
  TextBlobBuilder builder;
  const auto& buffer = builder.allocRunPosH(font, glyphCount, y);
  memcpy(buffer.glyphs, glyphIDs, glyphCount * sizeof(GlyphID));
  memcpy(buffer.positions, xPositions, glyphCount * sizeof(float));
  return builder.build();
}

std::shared_ptr<TextBlob> TextBlob::MakeFromRSXform(const GlyphID glyphIDs[],
                                                    const RSXform xforms[], size_t glyphCount,
                                                    const Font& font) {
  if (glyphCount == 0 || font.getTypeface() == nullptr) {
    return nullptr;
  }
  TextBlobBuilder builder;
  const auto& buffer = builder.allocRunRSXform(font, glyphCount);
  memcpy(buffer.glyphs, glyphIDs, glyphCount * sizeof(GlyphID));
  memcpy(buffer.positions, xforms, glyphCount * sizeof(RSXform));
  return builder.build();
}

TextBlob::TextBlob(size_t runCount) : runCount(runCount) {
}

TextBlob::TextBlob(size_t runCount, const Rect& bounds)
    : runCount(runCount), bounds(new Rect(bounds)) {
}

TextBlob::~TextBlob() {
  AtomicCacheReset(bounds);
  // Explicitly destruct RunRecords since they contain Font objects
  const RunRecord* run = firstRun();
  for (size_t i = 0; i < runCount; i++) {
    const RunRecord* nextRun = run->next();
    run->~RunRecord();
    run = nextRun;
  }
}

void TextBlob::operator delete(void* p) {
  free(p);
}

void* TextBlob::operator new(size_t, void* p) {
  return p;
}

const RunRecord* TextBlob::firstRun() const {
  if (runCount == 0) {
    return nullptr;
  }
  // Run data immediately follows the TextBlob object, aligned for RunRecord.
  // This matches AlignedBlobHeaderSize() in TextBlobBuilder.
  size_t headerSize = sizeof(TextBlob);
  size_t alignment = alignof(RunRecord);
  size_t alignedOffset = (headerSize + alignment - 1) & ~(alignment - 1);
  return reinterpret_cast<const RunRecord*>(reinterpret_cast<const uint8_t*>(this) + alignedOffset);
}

TextBlob::Iterator TextBlob::begin() const {
  return Iterator(firstRun(), runCount);
}

GlyphRun TextBlob::Iterator::operator*() const {
  GlyphRun run;
  run.font = current->font;
  run.glyphCount = current->glyphCount;
  run.glyphs = current->glyphBuffer();
  run.positioning = current->positioning;
  run.positions = current->posBuffer();
  run.offsetY = current->y;
  return run;
}

TextBlob::Iterator& TextBlob::Iterator::operator++() {
  current = current->next();
  --remaining;
  return *this;
}

Rect TextBlob::getBounds() const {
  if (auto cachedBounds = AtomicCacheGet(bounds)) {
    return *cachedBounds;
  }
  auto totalBounds = computeBounds();
  AtomicCacheSet(bounds, &totalBounds);
  return totalBounds;
}

Rect TextBlob::computeBounds() const {
  Rect finalBounds = {};
  Matrix transformMat = {};
  for (auto run : *this) {
    auto& font = run.font;
    transformMat.reset();
    transformMat.setScale(font.getSize(), font.getSize());
    if (font.isFauxItalic()) {
      transformMat.postSkew(ITALIC_SKEW, 0.0f);
    }
    auto typeface = font.getTypeface();
    if (typeface == nullptr || typeface->getBounds().isEmpty()) {
      finalBounds.setEmpty();
      break;
    }
    auto fontBounds = typeface->getBounds();
    if (font.isFauxBold()) {
      auto fauxBoldScale = FauxBoldScale(font.getSize());
      fontBounds.outset(fauxBoldScale, fauxBoldScale);
    }
    transformMat.mapRect(&fontBounds);
    Rect runBounds = {};
    bool hasOnlyOffset = !HasComplexTransform(run);
    for (size_t i = 0; i < run.glyphCount; i++) {
      Rect glyphBounds = {};
      if (hasOnlyOffset) {
        auto position = GetGlyphPosition(run, i);
        glyphBounds = fontBounds.makeOffset(position.x, position.y);
      } else {
        glyphBounds = GetGlyphMatrix(run, i).mapRect(fontBounds);
      }
      if (i == 0) {
        runBounds = glyphBounds;
      } else {
        runBounds.join(glyphBounds);
      }
    }
    if (run.glyphCount == 0) {
      continue;
    }
    finalBounds.join(runBounds);
  }
  if (!finalBounds.isEmpty()) {
    return finalBounds;
  }
  return getTightBounds();
}

Rect TextBlob::getTightBounds(const Matrix* matrix) const {
  auto resolutionScale = matrix ? matrix->getMaxScale() : 1.0f;
  if (FloatNearlyZero(resolutionScale)) {
    return {};
  }
  auto hasScale = !FloatNearlyEqual(resolutionScale, 1.0f);
  auto inverseScale = 1.0f / resolutionScale;
  Rect totalBounds = {};
  for (auto run : *this) {
    auto font = run.font;
    if (hasScale) {
      font = font.makeWithSize(resolutionScale * font.getSize());
    }
    for (size_t i = 0; i < run.glyphCount; i++) {
      auto glyphBounds = font.getBounds(run.glyphs[i]);
      auto glyphMatrix = GetGlyphMatrix(run, i);
      if (hasScale) {
        // Pre-scale to counteract the enlarged glyphBounds from the scaled font.
        glyphMatrix.preScale(inverseScale, inverseScale);
      }
      glyphBounds = glyphMatrix.mapRect(glyphBounds);
      totalBounds.join(glyphBounds);
    }
  }
  if (matrix) {
    totalBounds = matrix->mapRect(totalBounds);
  }
  return totalBounds;
}

bool TextBlob::hitTestPoint(float localX, float localY, const Stroke* stroke) const {
  for (auto run : *this) {
    auto& font = run.font;
    auto usePathHitTest = font.hasOutlines();
    for (size_t i = 0; i < run.glyphCount; i++) {
      auto glyphMatrix = GetGlyphMatrix(run, i);
      Matrix inverseMatrix = {};
      if (!glyphMatrix.invert(&inverseMatrix)) {
        continue;
      }
      Point localPoint = {};
      inverseMatrix.mapXY(localX, localY, &localPoint);
      if (usePathHitTest) {
        Path glyphPath = {};
        if (font.getPath(run.glyphs[i], &glyphPath)) {
          if (stroke) {
            stroke->applyToPath(&glyphPath);
          }
          if (glyphPath.contains(localPoint.x, localPoint.y)) {
            return true;
          }
        }
      } else {
        auto glyphBounds = font.getBounds(run.glyphs[i]);
        if (stroke) {
          ApplyStrokeToBounds(*stroke, &glyphBounds);
        }
        if (glyphBounds.contains(localPoint.x, localPoint.y)) {
          return true;
        }
      }
    }
  }
  return false;
}

}  // namespace tgfx
