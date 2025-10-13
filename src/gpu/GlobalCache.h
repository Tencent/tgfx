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

#pragma once

#include <array>
#include <list>
#include <unordered_map>
#include "core/utils/SlidingWindowTracker.h"
#include "gpu/proxies/GPUBufferProxy.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/resources/Program.h"
#include "tgfx/core/Color.h"

namespace tgfx {
/**
 * GlobalCache manages GPU resources that need to stay alive for the lifetime of the Context.
 */
class GlobalCache {
 public:
  explicit GlobalCache(Context* context);

  /**
   * Finds a program in the cache by its key. Returns nullptr if no program is found. The program
   * will be kept alive for the lifetime of the GlobalCache.
   */
  std::shared_ptr<Program> findProgram(const BytesKey& programKey);

  /**
   * Find or creates a uniform GPUBuffer with specified size. If a suitable buffer already exists,
   * it will be reused. If no suitable buffer exists, a new buffer will be created.
   * The buffer will be kept alive for the lifetime of the GlobalCache.
   * Returns last buffer offset within the returned buffer via lastBufferOffset parameter.
   * Returns nullptr if the requested buffer size exceeds the maximum allowed uniform buffer size.
   */
  std::shared_ptr<GPUBuffer> findOrCreateUniformBuffer(size_t bufferSize, size_t* lastBufferOffset);

  /**
   * After calling Context::flush(), the cached buffer can be reused once the GPU has
   * finished processing commands for the current frame.
   * Typically, this is done at the start of each frame to allow buffer reuse.
   * This approach minimizes buffer creation and destruction, improving performance.
   */
  void resetUniformBuffer();

  /**
   * Adds a program to the cache with the specified key. If a program with the same key already
   * exists, it will be replaced with the new program.
   */
  void addProgram(const BytesKey& programKey, std::shared_ptr<Program> program);

  /**
   * Returns a texture that represents a gradient created from the specified colors and positions.
   */
  std::shared_ptr<TextureProxy> getGradient(const Color* colors, const float* positions, int count);

  /**
   * Returns a GPU buffer that contains indices for rendering a quad with or without antialiasing.
   */
  std::shared_ptr<GPUBufferProxy> getRectIndexBuffer(bool antialias);

  /**
   * Returns a GPU buffer containing indices for rendering a rounded rectangle, either for filling
   * or stroking.
   */
  std::shared_ptr<GPUBufferProxy> getRRectIndexBuffer(bool stroke);

  /**
   * Finds a static resource in the cache by its unique key. Returns nullptr if no resource is found.
   * The resource will be kept alive for the lifetime of the GlobalCache.
   */
  std::shared_ptr<Resource> findStaticResource(const UniqueKey& uniqueKey);

  /**
   * Adds a static resource to the cache. If a resource with the same unique key already exists,
   * it will be replaced with the new resource. The resource will be kept alive for the lifetime of
   * the GlobalCache.
   */
  void addStaticResource(const UniqueKey& uniqueKey, std::shared_ptr<Resource> resource);

 private:
  struct GradientTexture {
    GradientTexture(std::shared_ptr<TextureProxy> textureProxy, BytesKey gradientKey)
        : textureProxy(std::move(textureProxy)), gradientKey(std::move(gradientKey)) {
    }

    std::shared_ptr<TextureProxy> textureProxy = nullptr;
    BytesKey gradientKey = {};
    std::list<GradientTexture*>::iterator cachedPosition = {};
  };

  struct UniformBufferPacket {
    std::vector<std::shared_ptr<GPUBuffer>> gpuBuffers = {};
    size_t bufferIndex = 0;
    size_t cursor = 0;
  };

  Context* context = nullptr;
  std::list<Program*> programLRU = {};
  BytesKeyMap<std::shared_ptr<Program>> programMap = {};
  std::list<GradientTexture*> gradientLRU = {};
  BytesKeyMap<std::unique_ptr<GradientTexture>> gradientTextures = {};
  std::shared_ptr<GPUBufferProxy> aaQuadIndexBuffer = nullptr;
  std::shared_ptr<GPUBufferProxy> nonAAQuadIndexBuffer = nullptr;
  std::shared_ptr<GPUBufferProxy> rRectFillIndexBuffer = nullptr;
  std::shared_ptr<GPUBufferProxy> rRectStrokeIndexBuffer = nullptr;
  ResourceKeyMap<std::shared_ptr<Resource>> staticResources = {};
  // Triple buffering for uniform buffer management
  static constexpr uint32_t UNIFORM_BUFFER_COUNT = 3;
  std::array<UniformBufferPacket, UNIFORM_BUFFER_COUNT> tripleUniformBuffer = {};
  uint32_t tripleUniformBufferIndex = 0;
  uint64_t counter = 0;
  std::shared_ptr<SlidingWindowTracker> maxUniformBufferTracker = nullptr;
};
}  // namespace tgfx
