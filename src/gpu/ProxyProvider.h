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

#pragma once

#include "core/DataProvider.h"
#include "core/ImageDecoder.h"
#include "gpu/proxies/GpuBufferProxy.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "gpu/proxies/TextureProxy.h"
#include "tgfx/core/ImageGenerator.h"

namespace tgfx {
/**
 * A factory for creating proxy-derived objects.
 */
class ProxyProvider {
 public:
  explicit ProxyProvider(Context* context);

  /**
   * Returns true if the proxy provider has a proxy for the given ResourceKey.
   */
  bool hasResourceProxy(const ResourceKey& resourceKey);

  std::shared_ptr<GpuBufferProxy> createGpuBufferProxy(const ResourceKey& resourceKey,
                                                       std::shared_ptr<Data> data,
                                                       BufferType bufferType,
                                                       uint32_t renderFlags = 0);

  std::shared_ptr<GpuBufferProxy> createGpuBufferProxy(const ResourceKey& resourceKey,
                                                       std::shared_ptr<DataProvider> provider,
                                                       BufferType bufferType,
                                                       uint32_t renderFlags = 0);

  /*
   * Creates a TextureProxy for the given ImageBuffer. The image buffer will be released after being
   * uploaded to the GPU.
   */
  std::shared_ptr<TextureProxy> createTextureProxy(const ResourceKey& resourceKey,
                                                   std::shared_ptr<ImageBuffer> imageBuffer,
                                                   bool mipmapped = false,
                                                   uint32_t renderFlags = 0);

  /*
   * Creates a TextureProxy for the given ImageGenerator.
   */
  std::shared_ptr<TextureProxy> createTextureProxy(const ResourceKey& resourceKey,
                                                   std::shared_ptr<ImageGenerator> generator,
                                                   bool mipmapped = false,
                                                   uint32_t renderFlags = 0);

  /**
   * Creates a TextureProxy for the given ImageDecoder.
   */
  std::shared_ptr<TextureProxy> createTextureProxy(const ResourceKey& resourceKey,
                                                   std::shared_ptr<ImageDecoder> decoder,
                                                   bool mipmapped = false,
                                                   uint32_t renderFlags = 0);

  /**
   * Creates an empty TextureProxy with specified width, height, format, mipmap state and origin.
   */
  std::shared_ptr<TextureProxy> createTextureProxy(const ResourceKey& resourceKey, int width,
                                                   int height, PixelFormat format,
                                                   bool mipmapped = false,
                                                   ImageOrigin origin = ImageOrigin::TopLeft,
                                                   uint32_t renderFlags = 0);

  /**
   * Creates a TextureProxy for the provided BackendTexture. If adopted is true, the backend
   * texture will be destroyed at a later point after the proxy is released.
   */
  std::shared_ptr<TextureProxy> wrapBackendTexture(const BackendTexture& backendTexture,
                                                   ImageOrigin origin = ImageOrigin::TopLeft,
                                                   bool adopted = false);
  /**
   * Creates an empty RenderTargetProxy with specified width, height, format, sample count,
   * mipmap state and origin.
   */
  std::shared_ptr<RenderTargetProxy> createRenderTargetProxy(
      std::shared_ptr<TextureProxy> textureProxy, PixelFormat format, int sampleCount = 1);

  /**
   * Creates a render target proxy for the given BackendRenderTarget.
   */
  std::shared_ptr<RenderTargetProxy> wrapBackendRenderTarget(
      const BackendRenderTarget& backendRenderTarget, ImageOrigin origin = ImageOrigin::TopLeft);

  /*
   * Purges all unreferenced proxies.
   */
  void purgeExpiredProxies();

 private:
  Context* context = nullptr;
  std::unordered_map<uint64_t, std::weak_ptr<ResourceProxy>> proxyMap = {};

  static ResourceKey GetStrongKey(const ResourceKey& resourceKey, uint32_t renderFlags);

  std::shared_ptr<GpuBufferProxy> findGpuBufferProxy(const ResourceKey& resourceKey);

  std::shared_ptr<TextureProxy> findTextureProxy(const ResourceKey& resourceKey);

  std::shared_ptr<ResourceProxy> findResourceProxy(const ResourceKey& resourceKey);

  void addResourceProxy(std::shared_ptr<ResourceProxy> proxy, ResourceKey strongKey,
                        uint64_t domainID);
};
}  // namespace tgfx
