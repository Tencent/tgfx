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

static constexpr uint16_t VerticesPerNonAAQuad = 4;
static constexpr uint16_t IndicesPerNonAAQuad = 6;

// clang-format off
static constexpr uint16_t NonAAQuadIndexPattern[] = {
  0, 1, 2, 2, 1, 3
};
// clang-format on

std::shared_ptr<GpuBufferProxy> ResourceProvider::nonAAQuadIndexBuffer() {
  if (_nonAAQuadIndexBuffer == nullptr) {
    auto provider = std::make_unique<PatternedIndexBufferProvider>(
        NonAAQuadIndexPattern, IndicesPerNonAAQuad, RectDrawOp::MaxNumRects, VerticesPerNonAAQuad);
    _nonAAQuadIndexBuffer =
        GpuBufferProxy::MakeFrom(context, std::move(provider), BufferType::Index, 0);
  }
  return _nonAAQuadIndexBuffer;
}

uint16_t ResourceProvider::NumIndicesPerNonAAQuad() {
  return IndicesPerNonAAQuad;
}

static constexpr uint16_t VerticesPerAAQuad = 8;
static constexpr uint16_t IndicesPerAAQuad = 30;

// clang-format off
static constexpr uint16_t AAQuadIndexPattern[] = {
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
        AAQuadIndexPattern, IndicesPerAAQuad, RectDrawOp::MaxNumRects, VerticesPerAAQuad);
    _aaQuadIndexBuffer =
        GpuBufferProxy::MakeFrom(context, std::move(provider), BufferType::Index, 0);
  }
  return _aaQuadIndexBuffer;
}

uint16_t ResourceProvider::NumIndicesPerAAQuad() {
  return IndicesPerAAQuad;
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

static constexpr int OverstrokeIndicesCount = 6 * 4;
static constexpr int CornerIndicesCount = 6 * 4;
static constexpr int EdgeIndicesCount = 6 * 4;
static constexpr int CenterIndicesCount = 6;

// fill and standard stroke indices skip the overstroke "ring"
static const uint16_t* gStandardRRectIndices = gOverstrokeRRectIndices + OverstrokeIndicesCount;

// overstroke count is arraysize minus the center indices
// static constexpr int IndicesPerOverstrokeRRect =
//    OverstrokeIndicesCount + CornerIndicesCount + EdgeIndicesCount;
// fill count skips overstroke indices and includes center
static constexpr size_t kIndicesPerFillRRect =
    CornerIndicesCount + EdgeIndicesCount + CenterIndicesCount;
// stroke count is fill count minus center indices
static constexpr int kIndicesPerStrokeRRect = CornerIndicesCount + EdgeIndicesCount;

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
  // Use `auto&` to bind `indexBuffer` to the appropriate member buffer
  // (`_rRectFillIndexBuffer` or `_rRectStrokeIndexBuffer`) so that it gets
  // updated and cached directly.
  auto& indexBuffer = type == RRectType::FillType ? _rRectFillIndexBuffer : _rRectStrokeIndexBuffer;
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

void ResourceProvider::releaseAll() {
  if (_gradientCache) {
    _gradientCache->releaseAll();
  }
  _aaQuadIndexBuffer = nullptr;
  _nonAAQuadIndexBuffer = nullptr;
}
}  // namespace tgfx
