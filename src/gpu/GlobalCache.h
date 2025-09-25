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

#include <list>
#include <unordered_map>
#include "gpu/proxies/IndexBufferProxy.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/resources/Program.h"

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
  std::shared_ptr<IndexBufferProxy> getRectIndexBuffer(bool antialias);

  /**
   * Returns a GPU buffer containing indices for rendering a rounded rectangle, either for filling
   * or stroking.
   */
  std::shared_ptr<IndexBufferProxy> getRRectIndexBuffer(bool stroke);

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

  Context* context = nullptr;
  std::list<Program*> programLRU = {};
  BytesKeyMap<std::shared_ptr<Program>> programMap = {};
  std::list<GradientTexture*> gradientLRU = {};
  BytesKeyMap<std::unique_ptr<GradientTexture>> gradientTextures = {};
  std::shared_ptr<IndexBufferProxy> aaQuadIndexBuffer = nullptr;
  std::shared_ptr<IndexBufferProxy> nonAAQuadIndexBuffer = nullptr;
  std::shared_ptr<IndexBufferProxy> rRectFillIndexBuffer = nullptr;
  std::shared_ptr<IndexBufferProxy> rRectStrokeIndexBuffer = nullptr;
  ResourceKeyMap<std::shared_ptr<Resource>> staticResources = {};
};
}  // namespace tgfx
