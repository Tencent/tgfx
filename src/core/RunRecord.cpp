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

#include "core/RunRecord.h"
#include <cstring>
#include "core/GlyphRun.h"

namespace tgfx {

size_t RunRecord::StorageSize(size_t count, GlyphPositionMode mode) {
  auto scalars = ScalarsPerGlyph(mode);
  size_t size = sizeof(RunRecord);
  size_t glyphSize = count * sizeof(GlyphID);
  size += (glyphSize + 3) & ~static_cast<size_t>(3);
  size += count * scalars * sizeof(float);
  return (size + sizeof(void*) - 1) & ~(sizeof(void*) - 1);
}

GlyphID* RunRecord::glyphBuffer() {
  return reinterpret_cast<GlyphID*>(reinterpret_cast<uint8_t*>(this) + sizeof(RunRecord));
}

const GlyphID* RunRecord::glyphBuffer() const {
  return reinterpret_cast<const GlyphID*>(reinterpret_cast<const uint8_t*>(this) +
                                          sizeof(RunRecord));
}

float* RunRecord::posBuffer() {
  size_t glyphSize = glyphCount * sizeof(GlyphID);
  size_t alignedGlyphSize = (glyphSize + 3) & ~static_cast<size_t>(3);
  return reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(this) + sizeof(RunRecord) +
                                  alignedGlyphSize);
}

const float* RunRecord::posBuffer() const {
  size_t glyphSize = glyphCount * sizeof(GlyphID);
  size_t alignedGlyphSize = (glyphSize + 3) & ~static_cast<size_t>(3);
  return reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(this) + sizeof(RunRecord) +
                                        alignedGlyphSize);
}

size_t RunRecord::storageSize() const {
  return StorageSize(glyphCount, positionMode);
}

const RunRecord* RunRecord::next() const {
  return reinterpret_cast<const RunRecord*>(reinterpret_cast<const uint8_t*>(this) + storageSize());
}

void RunRecord::grow(uint32_t count) {
  auto scalars = ScalarsPerGlyph(positionMode);
  float* oldPos = posBuffer();
  uint32_t oldCount = glyphCount;
  glyphCount += count;
  float* newPos = posBuffer();
  if (newPos != oldPos) {
    memmove(newPos, oldPos, oldCount * scalars * sizeof(float));
  }
}

}  // namespace tgfx
