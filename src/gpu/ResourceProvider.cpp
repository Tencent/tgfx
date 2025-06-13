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

#include "ResourceProvider.h"
#include "GradientCache.h"
#include "core/DataSource.h"
#include "core/utils/Log.h"
#include "ops/RRectDrawOp.h"
#include "ops/RectDrawOp.h"
#include "tgfx/core/Buffer.h"
#include "tgfx/core/Data.h"

namespace tgfx {
class PatternedIndexBufferProvider : public DataSource<Data> {
 public:
  PatternedIndexBufferProvider(const uint16_t* pattern, uint16_t patternSize, uint16_t reps,
                               uint16_t vertCount)
      : pattern(pattern), patternSize(patternSize), reps(reps), vertCount(vertCount) {
  }

  std::shared_ptr<Data> getData() const override {
    auto size = static_cast<size_t>(reps * patternSize * sizeof(uint16_t));
    Buffer buffer(size);
    if (buffer.isEmpty()) {
      return nullptr;
    }
    auto* data = reinterpret_cast<uint16_t*>(buffer.data());
    for (uint16_t i = 0; i < reps; ++i) {
      uint16_t baseIdx = i * patternSize;
      auto baseVert = static_cast<uint16_t>(i * vertCount);
      for (uint16_t j = 0; j < patternSize; ++j) {
        data[baseIdx + j] = baseVert + pattern[j];
      }
    }
    return buffer.release();
  }

 private:
  const uint16_t* pattern;
  uint16_t patternSize;
  uint16_t reps;
  uint16_t vertCount;
};

ResourceProvider::~ResourceProvider() {
  if (_gradientCache) {
    DEBUG_ASSERT(_gradientCache->empty());
  }
  DEBUG_ASSERT(_aaQuadIndexBuffer == nullptr);
  DEBUG_ASSERT(_nonAAQuadIndexBuffer == nullptr);
  delete _gradientCache;
}

std::shared_ptr<Texture> ResourceProvider::getGradient(const Color* colors, const float* positions,
                                                       int count) {
  if (_gradientCache == nullptr) {
    _gradientCache = new GradientCache();
  }
  return _gradientCache->getGradient(context, colors, positions, count);
}

static constexpr uint16_t kVerticesPerNonAAQuad = 4;
static constexpr uint16_t kIndicesPerNonAAQuad = 6;

// clang-format off
static constexpr uint16_t kNonAAQuadIndexPattern[] = {
  0, 1, 2, 2, 1, 3
};
// clang-format on

std::shared_ptr<GpuBufferProxy> ResourceProvider::nonAAQuadIndexBuffer() {
  if (_nonAAQuadIndexBuffer == nullptr) {
    auto provider = std::make_unique<PatternedIndexBufferProvider>(
        kNonAAQuadIndexPattern, kIndicesPerNonAAQuad, RectDrawOp::MaxNumRects,
        kVerticesPerNonAAQuad);
    _nonAAQuadIndexBuffer =
        GpuBufferProxy::MakeFrom(context, std::move(provider), BufferType::Index, 0);
  }
  return _nonAAQuadIndexBuffer;
}

uint16_t ResourceProvider::NumIndicesPerNonAAQuad() {
  return kIndicesPerNonAAQuad;
}

static constexpr uint16_t kVerticesPerAAQuad = 8;
static constexpr uint16_t kIndicesPerAAQuad = 30;

// clang-format off
static constexpr uint16_t kAAQuadIndexPattern[] = {
  0, 1, 2, 1, 3, 2,
  0, 4, 1, 4, 5, 1,
  0, 6, 4, 0, 2, 6,
  2, 3, 6, 3, 7, 6,
  1, 5, 3, 3, 5, 7,
};
// clang-format on

std::shared_ptr<GpuBufferProxy> ResourceProvider::aaQuadIndexBuffer() {
  if (_aaQuadIndexBuffer == nullptr) {
    auto provider = std::make_unique<PatternedIndexBufferProvider>(
        kAAQuadIndexPattern, kIndicesPerAAQuad, RectDrawOp::MaxNumRects, kVerticesPerAAQuad);
    _aaQuadIndexBuffer =
        GpuBufferProxy::MakeFrom(context, std::move(provider), BufferType::Index, 0);
  }
  return _aaQuadIndexBuffer;
}

uint16_t ResourceProvider::NumIndicesPerAAQuad() {
  return kIndicesPerAAQuad;
}

// clang-format off
static const uint16_t gOverstrokeRRectIndices[] = {
  // overstroke quads
  // we place this at the beginning so that we can skip these indices when rendering normally
  16, 17, 19, 16, 19, 18,
  19, 17, 23, 19, 23, 21,
  21, 23, 22, 21, 22, 20,
  22, 16, 18, 22, 18, 20,

  // corners
  0, 1, 5, 0, 5, 4,
  2, 3, 7, 2, 7, 6,
  8, 9, 13, 8, 13, 12,
  10, 11, 15, 10, 15, 14,

  // edges
  1, 2, 6, 1, 6, 5,
  4, 5, 9, 4, 9, 8,
  6, 7, 11, 6, 11, 10,
  9, 10, 14, 9, 14, 13,

  // center
  // we place this at the end so that we can ignore these indices when not rendering as filled
  5, 6, 10, 5, 10, 9,
};
// clang-format on

static constexpr int kOverstrokeIndicesCount = 6 * 4;
static constexpr int kCornerIndicesCount = 6 * 4;
static constexpr int kEdgeIndicesCount = 6 * 4;
static constexpr int kCenterIndicesCount = 6;

// fill and standard stroke indices skip the overstroke "ring"
static const uint16_t* gStandardRRectIndices = gOverstrokeRRectIndices + kOverstrokeIndicesCount;

// overstroke count is arraysize minus the center indices
// static constexpr int kIndicesPerOverstrokeRRect =
//    kOverstrokeIndicesCount + kCornerIndicesCount + kEdgeIndicesCount;
// fill count skips overstroke indices and includes center
static constexpr size_t kIndicesPerFillRRect =
    kCornerIndicesCount + kEdgeIndicesCount + kCenterIndicesCount;
// stroke count is fill count minus center indices
static constexpr int kIndicesPerStrokeRRect = kCornerIndicesCount + kEdgeIndicesCount;

class RRectIndicesProvider : public DataSource<Data> {
 public:
  explicit RRectIndicesProvider(size_t rectSize, RRectType type) : rectSize(rectSize), type(type) {
  }

  std::shared_ptr<Data> getData() const override {
    auto indicesCount = ResourceProvider::NumIndicesPerRRect(type);
    auto bufferSize = rectSize * indicesCount * sizeof(uint16_t);
    Buffer buffer(bufferSize);
    auto indices = reinterpret_cast<uint16_t*>(buffer.data());
    int index = 0;
    for (size_t i = 0; i < rectSize; ++i) {
      auto offset = static_cast<uint16_t>(i * 16);
      for (size_t j = 0; j < indicesCount; ++j) {
        indices[index++] = gStandardRRectIndices[j] + offset;
      }
    }
    return buffer.release();
  }

 private:
  size_t rectSize = 0;
  RRectType type = RRectType::FillType;
};

std::shared_ptr<GpuBufferProxy> ResourceProvider::rRectIndexBuffer(RRectType type) {
  auto indexBuffer = type == RRectType::FillType ? _rRectFillIndexBuffer : _rRectStrokeIndexBuffer;
  if (indexBuffer == nullptr) {
    auto provider = std::make_unique<RRectIndicesProvider>(RRectDrawOp::MaxNumRRects, type);
    indexBuffer = GpuBufferProxy::MakeFrom(context, std::move(provider), BufferType::Index, 0);
  }
  return indexBuffer;
}

uint16_t ResourceProvider::NumIndicesPerRRect(RRectType type) {
  switch (type) {
    case RRectType::FillType:
      return kIndicesPerFillRRect;
    case RRectType::StrokeType:
      return kIndicesPerStrokeRRect;
    default:
      break;
  }
  ABORT("Invalid type");
}

static const int kMiterStrokeRectIndexCnt = 3 * 24;
static const int kBevelStrokeRectIndexCnt = 48 + 36 + 24;
// clang-format off
static const uint16_t gMiterStrokeRectIndices[] = {
  0 + 0, 1 + 0, 5 + 0, 5 + 0, 4 + 0, 0 + 0,
  1 + 0, 2 + 0, 6 + 0, 6 + 0, 5 + 0, 1 + 0,
  2 + 0, 3 + 0, 7 + 0, 7 + 0, 6 + 0, 2 + 0,
  3 + 0, 0 + 0, 4 + 0, 4 + 0, 7 + 0, 3 + 0,

  0 + 4, 1 + 4, 5 + 4, 5 + 4, 4 + 4, 0 + 4,
  1 + 4, 2 + 4, 6 + 4, 6 + 4, 5 + 4, 1 + 4,
  2 + 4, 3 + 4, 7 + 4, 7 + 4, 6 + 4, 2 + 4,
  3 + 4, 0 + 4, 4 + 4, 4 + 4, 7 + 4, 3 + 4,

  0 + 8, 1 + 8, 5 + 8, 5 + 8, 4 + 8, 0 + 8,
  1 + 8, 2 + 8, 6 + 8, 6 + 8, 5 + 8, 1 + 8,
  2 + 8, 3 + 8, 7 + 8, 7 + 8, 6 + 8, 2 + 8,
  3 + 8, 0 + 8, 4 + 8, 4 + 8, 7 + 8, 3 + 8,
};
// clang-format on

/**
 * As in miter-stroke, index = a + b, and a is the current index, b is the shift
 * from the first index. The index layout:
 * outer AA line: 0~3, 4~7
 * outer edge:    8~11, 12~15
 * inner edge:    16~19
 * inner AA line: 20~23
 * Following comes a bevel-stroke rect and its indices:
 *
 *           4                                 7
 *            *********************************
 *          *   ______________________________  *
 *         *  / 12                          15 \  *
 *        *  /                                  \  *
 *     0 *  |8     16_____________________19  11 |  * 3
 *       *  |       |                    |       |  *
 *       *  |       |  ****************  |       |  *
 *       *  |       |  * 20        23 *  |       |  *
 *       *  |       |  *              *  |       |  *
 *       *  |       |  * 21        22 *  |       |  *
 *       *  |       |  ****************  |       |  *
 *       *  |       |____________________|       |  *
 *     1 *  |9    17                      18   10|  * 2
 *        *  \                                  /  *
 *         *  \13 __________________________14/  *
 *          *                                   *
 *           **********************************
 *          5                                  6
 */
// clang-format off
static const uint16_t gBevelStrokeRectIndices[] = {
  // Draw outer AA, from outer AA line to outer edge, shift is 0.
  0 + 0, 1 + 0,  9 + 0,  9 + 0,  8 + 0, 0 + 0,
  1 + 0, 5 + 0, 13 + 0, 13 + 0,  9 + 0, 1 + 0,
  5 + 0, 6 + 0, 14 + 0, 14 + 0, 13 + 0, 5 + 0,
  6 + 0, 2 + 0, 10 + 0, 10 + 0, 14 + 0, 6 + 0,
  2 + 0, 3 + 0, 11 + 0, 11 + 0, 10 + 0, 2 + 0,
  3 + 0, 7 + 0, 15 + 0, 15 + 0, 11 + 0, 3 + 0,
  7 + 0, 4 + 0, 12 + 0, 12 + 0, 15 + 0, 7 + 0,
  4 + 0, 0 + 0,  8 + 0,  8 + 0, 12 + 0, 4 + 0,

  // Draw the stroke, from outer edge to inner edge, shift is 8.
  0 + 8, 1 + 8, 9 + 8, 9 + 8, 8 + 8, 0 + 8,
  1 + 8, 5 + 8, 9 + 8,
  5 + 8, 6 + 8, 10 + 8, 10 + 8, 9 + 8, 5 + 8,
  6 + 8, 2 + 8, 10 + 8,
  2 + 8, 3 + 8, 11 + 8, 11 + 8, 10 + 8, 2 + 8,
  3 + 8, 7 + 8, 11 + 8,
  7 + 8, 4 + 8, 8 + 8, 8 + 8, 11 + 8, 7 + 8,
  4 + 8, 0 + 8, 8 + 8,

  // Draw the inner AA, from inner edge to inner AA line, shift is 16.
  0 + 16, 1 + 16, 5 + 16, 5 + 16, 4 + 16, 0 + 16,
  1 + 16, 2 + 16, 6 + 16, 6 + 16, 5 + 16, 1 + 16,
  2 + 16, 3 + 16, 7 + 16, 7 + 16, 6 + 16, 2 + 16,
  3 + 16, 0 + 16, 4 + 16, 4 + 16, 7 + 16, 3 + 16,
};
// clang-format on

std::shared_ptr<GpuBufferProxy> ResourceProvider::aaStrokeRectIndexBuffer(tgfx::LineJoin join) {
  if (_rectStrokeIndexBuffer == nullptr) {
    if (join == LineJoin::Round) {
      return nullptr;  // Round join is not supported for stroke rects.
    }
    bool isMiter = (join == LineJoin::Miter);
    auto provider = std::make_unique<PatternedIndexBufferProvider>(
        isMiter ? gMiterStrokeRectIndices : gBevelStrokeRectIndices, isMiter ? kMiterStrokeRectIndexCnt : kBevelStrokeRectIndexCnt, RectDrawOp::MaxNumRects,
        kVerticesPerNonAAQuad);
    _rectStrokeIndexBuffer =
        GpuBufferProxy::MakeFrom(context, std::move(provider), BufferType::Index, 0);
  }
  return _rectStrokeIndexBuffer;
}

uint16_t ResourceProvider::NumIndicesStrokeRect(LineJoin join) {
  switch (join) {
    case LineJoin::Miter:
      return kMiterStrokeRectIndexCnt;
    case LineJoin::Bevel:
      return kBevelStrokeRectIndexCnt;
    default:
      ABORT("Invalid join type");
      return 0;
  }
}

void ResourceProvider::releaseAll() {
  if (_gradientCache) {
    _gradientCache->releaseAll();
  }
  _aaQuadIndexBuffer = nullptr;
  _nonAAQuadIndexBuffer = nullptr;
}
}  // namespace tgfx
