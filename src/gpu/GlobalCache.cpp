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

#include "GlobalCache.h"
#include "AlignTo.h"
#include "core/GradientGenerator.h"
#include "core/PixelBuffer.h"
#include "gpu/ProxyProvider.h"
#include "gpu/ops/RRectDrawOp.h"
#include "gpu/ops/RectDrawOp.h"
#include "opengl/GLBuffer.h"
#include "tgfx/core/Buffer.h"

namespace tgfx {
static constexpr size_t MAX_PROGRAM_COUNT = 128;
static constexpr size_t MAX_NUM_CACHED_GRADIENT_BITMAPS = 32;
static constexpr uint16_t VERTICES_PER_NON_AA_QUAD = 4;
static constexpr uint16_t VERTICES_PER_AA_QUAD = 8;
static constexpr size_t MAX_UNIFORM_BUFFER_SIZE = 64 * 1024;
static constexpr uint16_t VERTICES_PER_AA_MITER_STROKE_RECT = 16;
static constexpr uint16_t VERTICES_PER_AA_BEVEL_STROKE_RECT = 24;
static constexpr uint16_t VERTICES_PER_AA_ROUND_STROKE_RECT = 24;
static constexpr uint16_t VERTICES_PER_NON_AA_MITER_STROKE_RECT = 8;
static constexpr uint16_t VERTICES_PER_NON_AA_BEVEL_STROKE_RECT = 12;
static constexpr uint16_t VERTICES_PER_NON_AA_ROUND_STROKE_RECT = 20;

// Hairline rendering constants
static constexpr uint32_t HAIRLINE_LINE_NUM_VERTICES = 6;
static constexpr uint32_t HAIRLINE_LINE_NUM_INDICES = 18;
static constexpr uint32_t HAIRLINE_QUAD_NUM_VERTICES = 5;
static constexpr uint32_t HAIRLINE_QUAD_NUM_INDICES = 9;

// clang-format off
static constexpr uint16_t HairlineLineIndexPattern[] = {
  0, 1, 3, 0, 3, 2, 0, 4, 5, 0, 5, 1, 0, 2, 4, 1, 5, 3
};

static constexpr uint16_t HairlineQuadIndexPattern[] = {
  0, 1, 2, 2, 4, 3, 1, 4, 2
};
// clang-format on

GlobalCache::GlobalCache(Context* context) : context(context) {
}

std::shared_ptr<Program> GlobalCache::findProgram(const BytesKey& programKey) {
  auto result = programMap.find(programKey);
  if (result != programMap.end()) {
    auto program = result->second;
    programLRU.erase(program->cachedPosition);
    programLRU.push_front(program.get());
    program->cachedPosition = programLRU.begin();
    return program;
  }
  return nullptr;
}

std::shared_ptr<GPUBuffer> GlobalCache::findOrCreateUniformBuffer(size_t bufferSize,
                                                                  size_t* lastBufferOffset) {
  auto maxUBOSize =
      std::max(static_cast<size_t>(context->shaderCaps()->maxUBOSize), MAX_UNIFORM_BUFFER_SIZE);
  auto uboOffsetAlignment = static_cast<size_t>(context->shaderCaps()->uboOffsetAlignment);

  if (maxUBOSize == 0) {
    LOGE("[GlobalCache::findOrCreateUniformBuffer] maxUBOSize is 0");
    return nullptr;
  }

  auto alignedBufferSize = AlignTo(bufferSize, uboOffsetAlignment);

  if (bufferSize == 0 || alignedBufferSize > maxUBOSize) {
    LOGE(
        "[GlobalCache::findOrCreateUniformBuffer] invalid request buffer size: %zu, max UBO "
        "size: %zu, %s:%d",
        bufferSize, maxUBOSize, __FILE__, __LINE__);
    return nullptr;
  }

  auto& uniformBufferPacket = tripleUniformBuffer[tripleUniformBufferIndex];

  auto getAverageUniformBufferSize = [this]() {
    size_t totalUniformBufferSize = 0;
    for (const auto& packet : tripleUniformBuffer) {
      totalUniformBufferSize += packet.gpuBuffers.size();
    }
    return (totalUniformBufferSize + UNIFORM_BUFFER_COUNT - 1) / UNIFORM_BUFFER_COUNT;
  };

  if (uniformBufferPacket.gpuBuffers.empty()) {
    auto buffer = context->gpu()->createBuffer(maxUBOSize, GPUBufferUsage::UNIFORM);
    if (buffer == nullptr) {
      LOGE(
          "[GlobalCache::findOrCreateUniformBuffer] failed to create initial uniform buffer, "
          "request buffer size: %zu, %s:%d",
          bufferSize, __FILE__, __LINE__);
      return nullptr;
    }
    uniformBufferPacket.gpuBuffers.emplace_back(std::move(buffer));
    uniformBufferPacket.bufferIndex = 0;
    uniformBufferPacket.cursor = 0;
    maxUniformBufferTracker.addValue(getAverageUniformBufferSize());
  }

  // Check if triple buffer has enough space
  if (uniformBufferPacket.bufferIndex < uniformBufferPacket.gpuBuffers.size() &&
      uniformBufferPacket.cursor + alignedBufferSize <=
          uniformBufferPacket.gpuBuffers[uniformBufferPacket.bufferIndex]->size()) {
    *lastBufferOffset = uniformBufferPacket.cursor;
    uniformBufferPacket.cursor += alignedBufferSize;
    return uniformBufferPacket.gpuBuffers[uniformBufferPacket.bufferIndex];
  }

  // Need to move to next buffer or create a new one
  uniformBufferPacket.bufferIndex++;
  uniformBufferPacket.cursor = 0;

  if (uniformBufferPacket.bufferIndex >= uniformBufferPacket.gpuBuffers.size()) {
    // Need to create a new buffer
    auto buffer = context->gpu()->createBuffer(maxUBOSize, GPUBufferUsage::UNIFORM);
    if (buffer == nullptr) {
      LOGE(
          "[GlobalCache::findOrCreateUniformBuffer] failed to create uniform buffer, request "
          "buffer size: %zu, %s:%d",
          bufferSize, __FILE__, __LINE__);
      return nullptr;
    }
    uniformBufferPacket.gpuBuffers.emplace_back(std::move(buffer));
    maxUniformBufferTracker.addValue(getAverageUniformBufferSize());
  }

  *lastBufferOffset = uniformBufferPacket.cursor;
  uniformBufferPacket.cursor += alignedBufferSize;

  return uniformBufferPacket.gpuBuffers[uniformBufferPacket.bufferIndex];
}

void GlobalCache::resetUniformBuffer() {
  counter++;
  tripleUniformBufferIndex = counter % UNIFORM_BUFFER_COUNT;
  if (counter == UNIFORM_BUFFER_COUNT) {
    counter = 0;
  }

  auto& currentBuffer = tripleUniformBuffer[tripleUniformBufferIndex];

  size_t maxReuseSize = maxUniformBufferTracker.getMaxValue();
  if (maxReuseSize > 0 && currentBuffer.gpuBuffers.size() > maxReuseSize) {
    currentBuffer.gpuBuffers.resize(maxReuseSize);
  }

  currentBuffer.bufferIndex = 0;
  currentBuffer.cursor = 0;
}

void GlobalCache::addProgram(const BytesKey& programKey, std::shared_ptr<Program> program) {
  if (program == nullptr) {
    return;
  }
  program->programKey = programKey;
  programLRU.push_front(program.get());
  program->cachedPosition = programLRU.begin();
  programMap[programKey] = std::move(program);
  while (programLRU.size() > MAX_PROGRAM_COUNT) {
    auto oldProgram = programLRU.back();
    programLRU.pop_back();
    programMap.erase(oldProgram->programKey);
  }
}

std::shared_ptr<TextureProxy> GlobalCache::getGradient(const Color* colors, const float* positions,
                                                       int count) {
  BytesKey bytesKey = {};
  for (int i = 0; i < count; ++i) {
    bytesKey.write(colors[i].red);
    bytesKey.write(colors[i].green);
    bytesKey.write(colors[i].blue);
    bytesKey.write(colors[i].alpha);
    bytesKey.write(positions[i]);
  }
  auto result = gradientTextures.find(bytesKey);
  if (result != gradientTextures.end()) {
    auto& texture = result->second;
    gradientLRU.erase(texture->cachedPosition);
    gradientLRU.push_front(texture.get());
    texture->cachedPosition = gradientLRU.begin();
    return texture->textureProxy;
  }
  auto generator = std::make_shared<GradientGenerator>(colors, positions, count);
  auto textureProxy = context->proxyProvider()->createTextureProxy(std::move(generator));
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto gradientTexture = std::make_unique<GradientTexture>(textureProxy, bytesKey);
  gradientLRU.push_front(gradientTexture.get());
  gradientTexture->cachedPosition = gradientLRU.begin();
  gradientTextures[bytesKey] = std::move(gradientTexture);
  while (gradientLRU.size() > MAX_NUM_CACHED_GRADIENT_BITMAPS) {
    auto texture = gradientLRU.back();
    gradientLRU.pop_back();
    gradientTextures.erase(texture->gradientKey);
  }
  return textureProxy;
}

// clang-format off
static constexpr uint16_t NonAAQuadIndexPattern[] = {
  0, 1, 2, 2, 1, 3
};

static constexpr uint16_t AAQuadIndexPattern[] = {
  0, 1, 2, 1, 3, 2,
  0, 4, 1, 4, 5, 1,
  0, 6, 4, 0, 2, 6,
  2, 3, 6, 3, 7, 6,
  1, 5, 3, 3, 5, 7,
};
// clang-format on

class RectIndicesProvider : public DataSource<Data> {
 public:
  RectIndicesProvider(const uint16_t* pattern, uint16_t patternSize, uint16_t reps,
                      uint16_t vertCount)
      : pattern(pattern), patternSize(patternSize), reps(reps), vertCount(vertCount) {
  }

  std::shared_ptr<Data> getData() const override {
    auto size = sizeof(uint16_t) * reps * patternSize;
    Buffer buffer(size);
    if (buffer.isEmpty()) {
      return nullptr;
    }
    auto data = reinterpret_cast<uint16_t*>(buffer.data());
    for (uint16_t i = 0; i < reps; ++i) {
      //Note: decltype(data[index]) resolves to uint16_t, while decltype(index) is size_t, so cast
      // 'i' to size_t before multiplication to prevent overflow when computing the array index.
      auto baseIdx = static_cast<size_t>(i) * patternSize;
      auto baseVert = static_cast<uint16_t>(i * vertCount);
      for (uint16_t j = 0; j < patternSize; ++j) {
        data[baseIdx + j] = baseVert + pattern[j];
      }
    }
    return buffer.release();
  }

 private:
  const uint16_t* pattern = nullptr;
  uint16_t patternSize = 0;
  uint16_t reps = 0;
  uint16_t vertCount = 0;
};

class HairlineIndicesProvider : public DataSource<Data> {
 public:
  HairlineIndicesProvider(const uint16_t* pattern, uint32_t patternSize, uint32_t reps,
                          uint32_t vertCount)
      : pattern(pattern), patternSize(patternSize), reps(reps), vertCount(vertCount) {
  }

  std::shared_ptr<Data> getData() const override {
    auto size = sizeof(uint32_t) * reps * patternSize;
    Buffer buffer(size);
    if (buffer.isEmpty()) {
      return nullptr;
    }
    auto data = reinterpret_cast<uint32_t*>(buffer.data());
    for (uint32_t i = 0; i < reps; ++i) {
      auto baseIdx = static_cast<size_t>(i) * patternSize;
      auto baseVert = i * vertCount;
      for (uint32_t j = 0; j < patternSize; ++j) {
        data[baseIdx + j] = baseVert + pattern[j];
      }
    }
    return buffer.release();
  }

 private:
  const uint16_t* pattern = nullptr;
  uint32_t patternSize = 0;
  uint32_t reps = 0;
  uint32_t vertCount = 0;
};

std::shared_ptr<GPUBufferProxy> GlobalCache::getRectIndexBuffer(
    bool antialias, const std::optional<LineJoin>& lineJoin) {
  if (!lineJoin) {
    if (antialias) {
      if (aaQuadIndexBuffer == nullptr) {
        auto provider =
            std::make_unique<RectIndicesProvider>(AAQuadIndexPattern, RectDrawOp::IndicesPerAAQuad,
                                                  RectDrawOp::MaxNumRects, VERTICES_PER_AA_QUAD);
        aaQuadIndexBuffer = context->proxyProvider()->createIndexBufferProxy(std::move(provider));
      }
      return aaQuadIndexBuffer;
    }
    if (nonAAQuadIndexBuffer == nullptr) {
      auto provider = std::make_unique<RectIndicesProvider>(
          NonAAQuadIndexPattern, RectDrawOp::IndicesPerNonAAQuad, RectDrawOp::MaxNumRects,
          VERTICES_PER_NON_AA_QUAD);
      nonAAQuadIndexBuffer = context->proxyProvider()->createIndexBufferProxy(std::move(provider));
    }
    return nonAAQuadIndexBuffer;
  }
  switch (*lineJoin) {
    case LineJoin::Miter:
      return getMiterStrokeIndexBuffer(antialias);
    case LineJoin::Round:
      return getRoundStrokeIndexBuffer(antialias);
    case LineJoin::Bevel:
      return getBevelStrokeIndexBuffer(antialias);
    default:
      return nullptr;
  }
}

// clang-format off
static const uint16_t OverstrokeRRectIndices[] = {
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
// fill and standard stroke indices skip the overstroke "ring"
static const uint16_t* StandardRRectIndices = OverstrokeRRectIndices + OverstrokeIndicesCount;

class AARRectIndicesProvider : public DataSource<Data> {
 public:
  explicit AARRectIndicesProvider(size_t rectSize, bool stroke)
      : rectSize(rectSize), stroke(stroke) {
  }

  std::shared_ptr<Data> getData() const override {
    auto indicesCount =
        stroke ? RRectDrawOp::IndicesPerAAStrokeRRect : RRectDrawOp::IndicesPerAAFillRRect;
    auto bufferSize = rectSize * indicesCount * sizeof(uint16_t);
    Buffer buffer(bufferSize);
    auto indices = reinterpret_cast<uint16_t*>(buffer.data());
    int index = 0;
    for (size_t i = 0; i < rectSize; ++i) {
      auto offset = static_cast<uint16_t>(i * 16);
      for (size_t j = 0; j < indicesCount; ++j) {
        indices[index++] = StandardRRectIndices[j] + offset;
      }
    }
    return buffer.release();
  }

 private:
  size_t rectSize = 0;
  bool stroke = false;
};

static constexpr uint16_t NonAARRectIndexPattern[] = {0, 1, 2, 0, 2, 3};

std::shared_ptr<GPUBufferProxy> GlobalCache::getRRectIndexBuffer(bool stroke, AAType aaType) {
  if (aaType == AAType::None) {
    if (nonAARRectIndexBuffer == nullptr) {
      auto provider = std::make_unique<RectIndicesProvider>(
          NonAARRectIndexPattern, RRectDrawOp::IndicesPerNonAARRect, RRectDrawOp::MaxNumRRects,
          RRectDrawOp::VerticesPerNonAARRect);
      nonAARRectIndexBuffer = context->proxyProvider()->createIndexBufferProxy(std::move(provider));
    }
    return nonAARRectIndexBuffer;
  }
  auto& indexBuffer = stroke ? rRectStrokeIndexBuffer : rRectFillIndexBuffer;
  if (indexBuffer == nullptr) {
    auto provider = std::make_unique<AARRectIndicesProvider>(RRectDrawOp::MaxNumRRects, stroke);
    indexBuffer = context->proxyProvider()->createIndexBufferProxy(std::move(provider));
  }
  return indexBuffer;
}

std::shared_ptr<Resource> GlobalCache::findStaticResource(const UniqueKey& uniqueKey) {
  auto result = staticResources.find(uniqueKey);
  return result != staticResources.end() ? result->second : nullptr;
}

void GlobalCache::addStaticResource(const UniqueKey& uniqueKey,
                                    std::shared_ptr<Resource> resource) {
  if (uniqueKey.empty()) {
    return;
  }
  staticResources[uniqueKey] = std::move(resource);
}

// clang-format off
/**
 * As in miter-stroke, index = a + b, and a is the current index, b is the shift
 * from the first index. The index layout:
 * outer AA line: 0~3
 * outer edge:    4~7
 * inner edge:    8~11
 * inner AA line: 12~15
 * Following comes an AA miter-stroke rect and its indices:
 *    0                                    2
 *      **********************************
 *      * 4────────────────────────────6 *
 *      * │                            │ *
 *      * │     8────────────────10    │ *
 *      * │     │  ************  │     │ *
 *      * │     │  *12      14*  │     │ *
 *      * │     │  *          *  │     │ *
 *      * │     │  *13      15*  │     │ *
 *      * │     │  ************  │     │ *
 *      * │     9────────────────11    │ *
 *      * │                            │ *
 *      * 5────────────────────────────7 *
 *      **********************************
 *    1                                    3
 */
static constexpr uint16_t AAMiterStrokeRectIndices[] = {
  0 + 0, 1 + 0, 5 + 0, 5 + 0, 4 + 0, 0 + 0,
  1 + 0, 3 + 0, 7 + 0, 7 + 0, 5 + 0, 1 + 0,
  3 + 0, 2 + 0, 6 + 0, 6 + 0, 7 + 0, 3 + 0,
  2 + 0, 0 + 0, 4 + 0, 4 + 0, 6 + 0, 2 + 0,

  0 + 4, 1 + 4, 5 + 4, 5 + 4, 4 + 4, 0 + 4,
  1 + 4, 3 + 4, 7 + 4, 7 + 4, 5 + 4, 1 + 4,
  3 + 4, 2 + 4, 6 + 4, 6 + 4, 7 + 4, 3 + 4,
  2 + 4, 0 + 4, 4 + 4, 4 + 4, 6 + 4, 2 + 4,

  0 + 8, 1 + 8, 5 + 8, 5 + 8, 4 + 8, 0 + 8,
  1 + 8, 3 + 8, 7 + 8, 7 + 8, 5 + 8, 1 + 8,
  3 + 8, 2 + 8, 6 + 8, 6 + 8, 7 + 8, 3 + 8,
  2 + 8, 0 + 8, 4 + 8, 4 + 8, 6 + 8, 2 + 8,
};

/**
 *  Following comes a non-AA miter-stroke rect and its indices:
 *    0────────────────────────────2
 *    │                            │
 *    │     4────────────────6     │
 *    │     |                │     │
 *    │     │                │     │
 *    │     │                │     │
 *    │     │                │     │
 *    │     5────────────────7     │
 *    │                            │
 *    1────────────────────────────3
*/
static constexpr uint16_t NonAAMiterStrokeRectIndices[] = {
  0, 1, 5, 5, 4, 0,
  1, 3, 7, 7, 5, 1,
  3, 2, 6, 6, 7, 3,
  2, 0, 4, 4, 6, 2,
};

/**
 * As in bevel-stroke, index = a + b, and a is the current index, b is the shift
 * from the first index. The index layout:
 * outer AA line: 0~3, 4~7
 * outer edge:    8~11, 12~15
 * inner edge:    16~19
 * inner AA line: 20~23
 * Following comes an AA bevel-stroke rect and its indices:
 *
 *           4                                 6
 *            *********************************
 *          *   ______________________________  *
 *         *  / 12                          14 \  *
 *        *  /                                  \  *
 *     0 *  |8     16_____________________18  10 |  * 2
 *       *  |       |                    |       |  *
 *       *  |       |  ****************  |       |  *
 *       *  |       |  * 20        22 *  |       |  *
 *       *  |       |  *              *  |       |  *
 *       *  |       |  * 21        23 *  |       |  *
 *       *  |       |  ****************  |       |  *
 *       *  |       |____________________|       |  *
 *     1 *  |9    17                      19   11|  * 3
 *        *  \                                  /  *
 *         *  \13 __________________________15/  *
 *          *                                   *
 *           **********************************
 *          5                                  7
 */
static constexpr uint16_t AABevelStrokeRectIndices[] = {
  // Draw outer AA, from outer AA line to outer edge, shift is 0.
  0 + 0, 1 + 0,  9 + 0,  9 + 0,  8 + 0, 0 + 0,
  1 + 0, 5 + 0, 13 + 0, 13 + 0,  9 + 0, 1 + 0,
  5 + 0, 7 + 0, 15 + 0, 15 + 0, 13 + 0, 5 + 0,
  7 + 0, 3 + 0, 11 + 0, 11 + 0, 15 + 0, 7 + 0,
  3 + 0, 2 + 0, 10 + 0, 10 + 0, 11 + 0, 3 + 0,
  2 + 0, 6 + 0, 14 + 0, 14 + 0, 10 + 0, 2 + 0,
  6 + 0, 4 + 0, 12 + 0, 12 + 0, 14 + 0, 6 + 0,
  4 + 0, 0 + 0,  8 + 0,  8 + 0, 12 + 0, 4 + 0,

  // Draw the stroke, from outer edge to inner edge, shift is 8.
  0 + 8, 1 + 8, 9 + 8, 9 + 8, 8 + 8, 0 + 8,
  1 + 8, 5 + 8, 9 + 8,
  5 + 8, 7 + 8, 11 + 8, 11 + 8, 9 + 8, 5 + 8,
  7 + 8, 3 + 8, 11 + 8,
  3 + 8, 2 + 8, 10 + 8, 10 + 8, 11 + 8, 3 + 8,
  2 + 8, 6 + 8, 10 + 8,
  6 + 8, 4 + 8, 8 + 8, 8 + 8, 10 + 8, 6 + 8,
  4 + 8, 0 + 8, 8 + 8,

  // Draw the inner AA, from inner edge to inner AA line, shift is 16.
  0 + 16, 1 + 16, 5 + 16, 5 + 16, 4 + 16, 0 + 16,
  1 + 16, 3 + 16, 7 + 16, 7 + 16, 5 + 16, 1 + 16,
  3 + 16, 2 + 16, 6 + 16, 6 + 16, 7 + 16, 3 + 16,
  2 + 16, 0 + 16, 4 + 16, 4 + 16, 6 + 16, 2 + 16,
};

/**
 * As in non-AA bevel-stroke
 * from the first index. The index layout:
 * outer AA edge: 0~3, 4~7
 * inner edge:    8~11
 * Following comes a non-AA bevel-stroke rect and its indices:
 *              ______________________________
 *            / 4                           6  \
 *           /                                  \
 *          |0     8 ____________________10    2 |
 *          |       |                    |       |
 *          |       |                    |       |
 *          |       |                    |       |
 *          |       |                    |       |
 *          |       |                    |       |
 *          |       |                    |       |
 *          |       |____________________|       |
 *          |1    9                       11   3 |
 *           \                                  /
 *            \ 5  __________________________ 7/
 */
static constexpr uint16_t NonAABevelStrokeRectIndices[] = {
  0, 1, 9, 9, 8, 0,
  1, 5, 9,
  5, 7, 11, 11, 9, 5,
  7, 3, 11,
  3, 2, 10, 10, 11, 3,
  2, 6, 10,
  6, 4, 8, 8, 10, 6,
  4, 0, 8,
};

/**
 * As in round-stroke.
 * We take all points for an anti-aliased (AA) stroke,
 * but only points 0~19 for a non-AA stroke.
 * Following comes a round-stroke rect and its indices:
 *
 *   0──1────────────────────────────2──3
 *   │  │                            │  │
 *   4──5────────────────────────────6──7
 *   │  │                            │  │
 *   │  │ 16───────────────────────18│  │
 *   │  │  │  *******************  │ │  │
 *   │  │  │  * 20           22 *  │ │  │
 *   │  │  │  *                 *  │ │  │
 *   │  │  │  *                 *  │ │  │
 *   │  │  │  * 21           23 *  │ │  │
 *   │  │  │  *******************  │ │  │
 *   │  │ 17───────────────────────19│  │
 *   8──9────────────────────────────10─11
 *   │  │                            │  │
 *  12──13──────────────────────────14──15
 */
static constexpr uint16_t RoundStrokeRectIndices[] = {
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

  //inner stroke rect
  5, 9, 17, 17, 16, 5,
  9, 10, 19, 19, 17, 9,
  10, 6, 18, 18, 19, 10,
  6, 5, 16, 16, 18, 6,

  //AA rect
  16, 17, 21, 21, 20, 16,
  17, 19, 23, 23, 21, 17,
  19, 18, 22, 22, 23, 19,
  18, 16, 20, 20, 22, 18
};
// clang-format on

std::shared_ptr<GPUBufferProxy> GlobalCache::getMiterStrokeIndexBuffer(bool antialias) {
  auto& indexBuffer = antialias ? aaRectMiterStrokeIndexBuffer : nonAARectMiterStrokeIndexBuffer;
  if (indexBuffer == nullptr) {
    auto pattern = antialias ? AAMiterStrokeRectIndices : NonAAMiterStrokeRectIndices;
    auto patternSize = antialias ? RectDrawOp::IndicesPerAAMiterStrokeRect
                                 : RectDrawOp::IndicesPerNonAAMiterStrokeRect;
    auto vertCount =
        antialias ? VERTICES_PER_AA_MITER_STROKE_RECT : VERTICES_PER_NON_AA_MITER_STROKE_RECT;
    auto provider = std::make_unique<RectIndicesProvider>(pattern, patternSize,
                                                          RectDrawOp::MaxNumRects, vertCount);
    indexBuffer = context->proxyProvider()->createIndexBufferProxy(std::move(provider));
  }
  return indexBuffer;
}

std::shared_ptr<GPUBufferProxy> GlobalCache::getBevelStrokeIndexBuffer(bool antialias) {
  auto& indexBuffer = antialias ? aaRectBevelStrokeIndexBuffer : nonAARectBevelStrokeIndexBuffer;
  if (indexBuffer == nullptr) {
    auto pattern = antialias ? AABevelStrokeRectIndices : NonAABevelStrokeRectIndices;
    auto patternSize = antialias ? RectDrawOp::IndicesPerAABevelStrokeRect
                                 : RectDrawOp::IndicesPerNonAABevelStrokeRect;
    auto vertCount =
        antialias ? VERTICES_PER_AA_BEVEL_STROKE_RECT : VERTICES_PER_NON_AA_BEVEL_STROKE_RECT;
    auto provider = std::make_unique<RectIndicesProvider>(pattern, patternSize,
                                                          RectDrawOp::MaxNumRects, vertCount);
    indexBuffer = context->proxyProvider()->createIndexBufferProxy(std::move(provider));
  }
  return indexBuffer;
}

std::shared_ptr<GPUBufferProxy> GlobalCache::getRoundStrokeIndexBuffer(bool antialias) {
  auto& indexBuffer = antialias ? aaRectRoundStrokeIndexBuffer : nonAARectRoundStrokeIndexBuffer;
  if (indexBuffer == nullptr) {
    auto pattern = RoundStrokeRectIndices;
    auto patternSize = antialias ? RectDrawOp::IndicesPerAARoundStrokeRect
                                 : RectDrawOp::IndicesPerNonAARoundStrokeRect;
    auto vertCount =
        antialias ? VERTICES_PER_AA_ROUND_STROKE_RECT : VERTICES_PER_NON_AA_ROUND_STROKE_RECT;
    auto provider = std::make_unique<RectIndicesProvider>(pattern, patternSize,
                                                          RectDrawOp::MaxNumRects, vertCount);
    indexBuffer = context->proxyProvider()->createIndexBufferProxy(std::move(provider));
  }
  return indexBuffer;
}

std::shared_ptr<GPUBufferProxy> GlobalCache::getHairlineLineIndexBuffer() {
  if (hairlineLineIndexBuffer == nullptr) {
    auto provider = std::make_unique<HairlineIndicesProvider>(
        HairlineLineIndexPattern, HAIRLINE_LINE_NUM_INDICES, MAX_NUM_HAIRLINE_LINES,
        HAIRLINE_LINE_NUM_VERTICES);
    hairlineLineIndexBuffer = context->proxyProvider()->createIndexBufferProxy(std::move(provider));
  }
  return hairlineLineIndexBuffer;
}

std::shared_ptr<GPUBufferProxy> GlobalCache::getHairlineQuadIndexBuffer() {
  if (hairlineQuadIndexBuffer == nullptr) {
    auto provider = std::make_unique<HairlineIndicesProvider>(
        HairlineQuadIndexPattern, HAIRLINE_QUAD_NUM_INDICES, MAX_NUM_HAIRLINE_QUADS,
        HAIRLINE_QUAD_NUM_VERTICES);
    hairlineQuadIndexBuffer = context->proxyProvider()->createIndexBufferProxy(std::move(provider));
  }
  return hairlineQuadIndexBuffer;
}
}  // namespace tgfx
