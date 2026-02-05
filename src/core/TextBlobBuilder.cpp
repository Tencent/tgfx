/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "tgfx/core/TextBlobBuilder.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "core/GlyphTransform.h"
#include "core/RunRecord.h"
#include "tgfx/core/TextBlob.h"

namespace tgfx {

static size_t AlignedBlobHeaderSize() {
  size_t size = sizeof(TextBlob);
  size_t alignment = alignof(RunRecord);
  return (size + alignment - 1) & ~(alignment - 1);
}

TextBlobBuilder::TextBlobBuilder() = default;

TextBlobBuilder::~TextBlobBuilder() {
  freeStorage();
}

void TextBlobBuilder::reset() {
  storage = nullptr;
  storageSize = 0;
  storageUsed = 0;
  runCount = 0;
  lastRunOffset = 0;
  currentBuffer = {};
  delete userBounds;
  userBounds = nullptr;
}

void TextBlobBuilder::freeStorage() {
  if (storage) {
    auto headerSize = AlignedBlobHeaderSize();
    auto* ptr = storage + headerSize;
    for (size_t i = 0; i < runCount; i++) {
      auto* run = reinterpret_cast<RunRecord*>(ptr);
      size_t runSize = run->storageSize();
      run->~RunRecord();
      ptr += runSize;
    }
    free(storage);
  }
  reset();
}

void TextBlobBuilder::reserve(size_t size) {
  if (storageUsed + size <= storageSize) {
    return;
  }
  if (runCount == 0 && storageUsed == 0) {
    storageUsed = AlignedBlobHeaderSize();
  }
  size_t newSize = storageSize == 0 ? 128 : storageSize;
  while (newSize < storageUsed + size) {
    newSize *= 2;
  }
  auto* newStorage = static_cast<uint8_t*>(realloc(storage, newSize));
  storage = newStorage;
  storageSize = newSize;
}

RunRecord* TextBlobBuilder::lastRun() {
  if (lastRunOffset == 0 && runCount == 0) {
    return nullptr;
  }
  return reinterpret_cast<RunRecord*>(storage + lastRunOffset);
}

bool TextBlobBuilder::tryMerge(const Font& font, GlyphPositioning positioning, size_t count,
                               Point offset) {
  if (runCount == 0) {
    return false;
  }
  auto* run = lastRun();
  if (run->font != font || run->positioning != positioning) {
    return false;
  }
  // Default positioning cannot be merged (different x starting positions)
  if (positioning == GlyphPositioning::Default) {
    return false;
  }
  if (positioning == GlyphPositioning::Horizontal) {
    if (run->offset.y != offset.y) {
      return false;
    }
  }
  if (count > UINT32_MAX - run->glyphCount) {
    return false;
  }
  size_t oldSize = run->storageSize();
  size_t newSize = RunRecord::StorageSize(run->glyphCount + count, positioning);
  size_t delta = newSize - oldSize;
  reserve(delta);
  run = lastRun();
  uint32_t preMergeCount = run->glyphCount;
  run->grow(static_cast<uint32_t>(count));
  currentBuffer.glyphs = run->glyphBuffer() + preMergeCount;
  auto scalars = ScalarsPerGlyph(positioning);
  if (scalars > 0) {
    currentBuffer.positions = run->posBuffer() + preMergeCount * scalars;
  } else {
    currentBuffer.positions = nullptr;
  }
  storageUsed += delta;
  return true;
}

const TextBlobBuilder::RunBuffer& TextBlobBuilder::allocRunInternal(const Font& font,
                                                                     size_t glyphCount,
                                                                     GlyphPositioning positioning,
                                                                     Point offset) {
  if (glyphCount == 0) {
    currentBuffer = {};
    return currentBuffer;
  }
  if (tryMerge(font, positioning, glyphCount, offset)) {
    return currentBuffer;
  }
  size_t runSize = RunRecord::StorageSize(glyphCount, positioning);
  reserve(runSize);
  lastRunOffset = storageUsed;
  auto* run = new (storage + storageUsed)
      RunRecord{font, positioning, static_cast<uint32_t>(glyphCount), offset};
  storageUsed += runSize;
  runCount++;
  currentBuffer.glyphs = run->glyphBuffer();
  auto scalars = ScalarsPerGlyph(positioning);
  if (scalars > 0) {
    currentBuffer.positions = run->posBuffer();
  } else {
    currentBuffer.positions = nullptr;
  }
  return currentBuffer;
}

const TextBlobBuilder::RunBuffer& TextBlobBuilder::allocRun(const Font& font, size_t glyphCount,
                                                            float x, float y) {
  return allocRunInternal(font, glyphCount, GlyphPositioning::Default, Point::Make(x, y));
}

const TextBlobBuilder::RunBuffer& TextBlobBuilder::allocRunPosH(const Font& font, size_t glyphCount,
                                                                float y) {
  return allocRunInternal(font, glyphCount, GlyphPositioning::Horizontal, Point::Make(0.0f, y));
}

const TextBlobBuilder::RunBuffer& TextBlobBuilder::allocRunPos(const Font& font,
                                                               size_t glyphCount) {
  return allocRunInternal(font, glyphCount, GlyphPositioning::Point, Point::Zero());
}

const TextBlobBuilder::RunBuffer& TextBlobBuilder::allocRunRSXform(const Font& font,
                                                                   size_t glyphCount) {
  return allocRunInternal(font, glyphCount, GlyphPositioning::RSXform, Point::Zero());
}

const TextBlobBuilder::RunBuffer& TextBlobBuilder::allocRunMatrix(const Font& font,
                                                                  size_t glyphCount) {
  return allocRunInternal(font, glyphCount, GlyphPositioning::Matrix, Point::Zero());
}

void TextBlobBuilder::setBounds(const Rect& bounds) {
  if (userBounds == nullptr) {
    userBounds = new Rect(bounds);
  } else {
    *userBounds = bounds;
  }
}

void TextBlobBuilder::trimLastRun(size_t glyphCount) {
  if (runCount == 0) {
    return;
  }
  auto* run = lastRun();
  if (glyphCount >= run->glyphCount) {
    return;
  }
  size_t oldSize = run->storageSize();
  auto scalars = ScalarsPerGlyph(run->positioning);
  if (scalars > 0) {
    float* oldPos = run->posBuffer();
    run->glyphCount = static_cast<uint32_t>(glyphCount);
    float* newPos = run->posBuffer();
    if (newPos != oldPos) {
      memmove(newPos, oldPos, glyphCount * scalars * sizeof(float));
    }
  } else {
    run->glyphCount = static_cast<uint32_t>(glyphCount);
  }
  size_t newSize = run->storageSize();
  storageUsed -= (oldSize - newSize);
}

std::shared_ptr<TextBlob> TextBlobBuilder::build() {
  if (runCount == 0 || storageUsed == 0) {
    freeStorage();
    return nullptr;
  }

  lastRun()->setLast();

  TextBlob* blob = nullptr;
  if (userBounds != nullptr) {
    blob = new (storage) TextBlob(runCount, *userBounds);
  } else {
    blob = new (storage) TextBlob(runCount);
  }

  auto result = std::shared_ptr<TextBlob>(blob, [](TextBlob* p) {
    p->~TextBlob();
    TextBlob::operator delete(p);
  });

  reset();

  return result;
}

}  // namespace tgfx
