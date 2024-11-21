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
#include "core/utils/Log.h"
#include "core/utils/Profiling.h"
#include "tgfx/core/Buffer.h"

namespace tgfx {
class PatternedIndexBufferProvider : public DataProvider {
 public:
  PatternedIndexBufferProvider(const uint16_t* pattern, uint16_t patternSize, uint16_t reps,
                               uint16_t vertCount)
      : pattern(pattern), patternSize(patternSize), reps(reps), vertCount(vertCount) {
  }

  std::shared_ptr<Data> getData() const override {
    TRACE_ZONE_SCOPED_N("PatternedIndexBufferProvider::getData");
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

std::shared_ptr<GpuBufferProxy> ResourceProvider::nonAAQuadIndexBuffer() {
  if (_nonAAQuadIndexBuffer == nullptr) {
    _nonAAQuadIndexBuffer = createNonAAQuadIndexBuffer();
  }
  return _nonAAQuadIndexBuffer;
}

std::shared_ptr<GpuBufferProxy> ResourceProvider::aaQuadIndexBuffer() {
  if (_aaQuadIndexBuffer == nullptr) {
    _aaQuadIndexBuffer = createAAQuadIndexBuffer();
  }
  return _aaQuadIndexBuffer;
}

static constexpr uint16_t kMaxNumNonAAQuads = 1 << 8;  // max possible: (1 << 14) - 1;
static constexpr uint16_t kVerticesPerNonAAQuad = 4;
static constexpr uint16_t kIndicesPerNonAAQuad = 6;

std::shared_ptr<GpuBufferProxy> ResourceProvider::createNonAAQuadIndexBuffer() {
  // clang-format off
  static constexpr uint16_t kNonAAQuadIndexPattern[] = {
    0, 1, 2, 2, 1, 3
  };
  // clang-format on
  auto provider = std::make_shared<PatternedIndexBufferProvider>(
      kNonAAQuadIndexPattern, kIndicesPerNonAAQuad, kMaxNumNonAAQuads, kVerticesPerNonAAQuad);
  return GpuBufferProxy::MakeFrom(context, std::move(provider), BufferType::Index, 0);
}

uint16_t ResourceProvider::MaxNumNonAAQuads() {
  return kMaxNumNonAAQuads;
}

uint16_t ResourceProvider::NumIndicesPerNonAAQuad() {
  return kIndicesPerNonAAQuad;
}

static constexpr uint16_t kMaxNumAAQuads = 1 << 6;  // max possible: (1 << 13) - 1;
static constexpr uint16_t kVerticesPerAAQuad = 8;
static constexpr uint16_t kIndicesPerAAQuad = 30;

std::shared_ptr<GpuBufferProxy> ResourceProvider::createAAQuadIndexBuffer() {
  // clang-format off
  static constexpr uint16_t kAAQuadIndexPattern[] = {
    0, 1, 2, 1, 3, 2,
    0, 4, 1, 4, 5, 1,
    0, 6, 4, 0, 2, 6,
    2, 3, 6, 3, 7, 6,
    1, 5, 3, 3, 5, 7,
  };
  // clang-format on
  auto provider = std::make_shared<PatternedIndexBufferProvider>(
      kAAQuadIndexPattern, kIndicesPerAAQuad, kMaxNumAAQuads, kVerticesPerAAQuad);
  return GpuBufferProxy::MakeFrom(context, std::move(provider), BufferType::Index, 0);
}

uint16_t ResourceProvider::MaxNumAAQuads() {
  return kMaxNumAAQuads;
}

uint16_t ResourceProvider::NumIndicesPerAAQuad() {
  return kIndicesPerAAQuad;
}

void ResourceProvider::releaseAll() {
  if (_gradientCache) {
    _gradientCache->releaseAll();
  }
  _aaQuadIndexBuffer = nullptr;
  _nonAAQuadIndexBuffer = nullptr;
}
}  // namespace tgfx
