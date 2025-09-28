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
  auto uboSupport = context->gpu()->caps()->shaderCaps()->uboSupport;
  auto maxUBOSize =
      uboSupport ? std::max(static_cast<size_t>(context->gpu()->caps()->shaderCaps()->maxUBOSize),
                            MAX_UNIFORM_BUFFER_SIZE)
                 : MAX_UNIFORM_BUFFER_SIZE;
  auto uboOffsetAlignment =
      static_cast<size_t>(context->gpu()->caps()->shaderCaps()->uboOffsetAlignment);

  if (uboSupport && maxUBOSize == 0) {
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

  if (maxUniformBufferTracker == nullptr) {
    maxUniformBufferTracker = std::make_shared<SlidingWindowTracker>(10);
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
    maxUniformBufferTracker->addValue(getAverageUniformBufferSize());
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
    maxUniformBufferTracker->addValue(getAverageUniformBufferSize());
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

  size_t maxReuseSize = maxUniformBufferTracker->getMaxValue();
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
    auto size = reps * patternSize * sizeof(uint16_t);
    Buffer buffer(size);
    if (buffer.isEmpty()) {
      return nullptr;
    }
    auto data = reinterpret_cast<uint16_t*>(buffer.data());
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
  const uint16_t* pattern = nullptr;
  uint16_t patternSize = 0;
  uint16_t reps = 0;
  uint16_t vertCount = 0;
};

std::shared_ptr<IndexBufferProxy> GlobalCache::getRectIndexBuffer(bool antialias) {
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

class RRectIndicesProvider : public DataSource<Data> {
 public:
  explicit RRectIndicesProvider(size_t rectSize, bool stroke) : rectSize(rectSize), stroke(stroke) {
  }

  std::shared_ptr<Data> getData() const override {
    auto indicesCount =
        stroke ? RRectDrawOp::IndicesPerStrokeRRect : RRectDrawOp::IndicesPerFillRRect;
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

std::shared_ptr<IndexBufferProxy> GlobalCache::getRRectIndexBuffer(bool stroke) {
  auto& indexBuffer = stroke ? rRectStrokeIndexBuffer : rRectFillIndexBuffer;
  if (indexBuffer == nullptr) {
    auto provider = std::make_unique<RRectIndicesProvider>(RRectDrawOp::MaxNumRRects, stroke);
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

}  // namespace tgfx
