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

#include "core/utils/BlockBuffer.h"
#include "core/utils/SlidingWindowTracker.h"
#include "gpu/AAType.h"
#include "gpu/BackingFit.h"
#include "gpu/VertexProvider.h"
#include "gpu/proxies/GPUBufferProxy.h"
#include "gpu/proxies/GPUShapeProxy.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/proxies/VertexBufferView.h"
#include "tgfx/core/ImageGenerator.h"
#include "tgfx/core/Shape.h"

namespace tgfx {
/**
 * A factory for creating proxy-derived objects.
 */
class ProxyProvider {
 public:
  explicit ProxyProvider(Context* context);

  /**
   * Returns the proxy for the given UniqueKey. If the proxy does not exist, returns nullptr.
   */
  std::shared_ptr<ResourceProxy> findProxy(const UniqueKey& uniqueKey);

  /**
   * Returns the texture proxy for the given UniqueKey. If the proxy doesn't exist but a texture
   * view with the same UniqueKey does, it wraps the texture view in a texture proxy and returns it.
   */
  std::shared_ptr<TextureProxy> findOrWrapTextureProxy(const UniqueKey& uniqueKey);

  /**
   * Creates a GPUBufferProxy for the given data source. The source will be released after being
   * uploaded to the GPU.
   */
  std::shared_ptr<GPUBufferProxy> createIndexBufferProxy(std::unique_ptr<DataSource<Data>> source,
                                                         uint32_t renderFlags = 0);

  /**
   * Creates a readback GPUBufferProxy of the given size. The buffer can be used to read data back
   * from the GPU.
   */
  std::shared_ptr<GPUBufferProxy> createReadbackBufferProxy(size_t size);

  /**
   * Creates a VertexBufferView from the given VertexProvider. The provider will be released
   * after being uploaded to the GPU.
   */
  std::shared_ptr<VertexBufferView> createVertexBufferProxy(PlacementPtr<VertexProvider> provider,
                                                            uint32_t renderFlags = 0);

  /**
   * Creates a GPUShapeProxy for the given Shape. The shape will be released after being uploaded to
   * the GPU.
   */
  std::shared_ptr<GPUShapeProxy> createGPUShapeProxy(std::shared_ptr<Shape> shape, AAType aaType,
                                                     const Rect& clipBounds,
                                                     uint32_t renderFlags = 0);

  /*
   * Creates a TextureProxy for the given ImageBuffer. The image buffer will be released after being
   * uploaded to the GPU.
   */
  std::shared_ptr<TextureProxy> createTextureProxy(std::shared_ptr<ImageBuffer> imageBuffer,
                                                   bool mipmapped = false);

  /*
   * Creates a TextureProxy for the given ImageGenerator.
   */
  std::shared_ptr<TextureProxy> createTextureProxy(std::shared_ptr<ImageGenerator> generator,
                                                   bool mipmapped = false,
                                                   uint32_t renderFlags = 0);

  /**
   * Creates a TextureProxy for the given image source.
   */
  std::shared_ptr<TextureProxy> createTextureProxy(std::shared_ptr<DataSource<ImageBuffer>> source,
                                                   int width, int height, bool alphaOnly,
                                                   bool mipmapped = false);

  /**
   * Creates an empty TextureProxy with specified width, height, format, mipmap state and origin.
   */
  std::shared_ptr<TextureProxy> createTextureProxy(
      const UniqueKey& uniqueKey, int width, int height, PixelFormat format, bool mipmapped = false,
      ImageOrigin origin = ImageOrigin::TopLeft,
      std::shared_ptr<ColorSpace> colorSpace = ColorSpace::MakeSRGB(),
      BackingFit backingFit = BackingFit::Exact, uint32_t renderFlags = 0);

  /**
   * Creates a TextureProxy for the specified HardwareBuffer. Returns nullptr if
   * the hardware buffer is not supported on the current platform.
   */
  std::shared_ptr<TextureProxy> createTextureProxy(
      HardwareBufferRef hardwareBuffer, YUVColorSpace colorSpace = YUVColorSpace::BT601_LIMITED);

  /**
   * Creates a texture proxy for the provided BackendTexture. If adopted is true, the backend
   * texture will be destroyed at a later point after the texture proxy is released.
   */
  std::shared_ptr<TextureProxy> wrapExternalTexture(
      const BackendTexture& backendTexture, ImageOrigin origin = ImageOrigin::TopLeft,
      bool adopted = false, std::shared_ptr<ColorSpace> colorSpace = ColorSpace::MakeSRGB());

  /**
   * Creates a RenderTargetProxy for the specified BackendTexture, sample count, origin, and
   * adopted state. Returns nullptr if the backend texture is not renderable.
   */
  std::shared_ptr<RenderTargetProxy> createRenderTargetProxy(
      const BackendTexture& backendTexture, int sampleCount = 1,
      ImageOrigin origin = ImageOrigin::TopLeft, bool adopted = false,
      std::shared_ptr<ColorSpace> colorSpace = nullptr);

  /**
   * Creates a RenderTargetProxy for the specified HardwareBuffer and sample count. Returns nullptr
   * if the hardware buffer is not renderable.
   */
  std::shared_ptr<RenderTargetProxy> createRenderTargetProxy(
      HardwareBufferRef hardwareBuffer, int sampleCount = 1,
      std::shared_ptr<ColorSpace> colorSpace = nullptr);

  /**
   * Creates a RenderTargetProxy with specified width, height, format, sample count, mipmap state
   * and origin. Returns nullptr if the format is not renderable.
   */
  std::shared_ptr<RenderTargetProxy> createRenderTargetProxy(
      const UniqueKey& uniqueKey, int width, int height, PixelFormat format, int sampleCount = 1,
      bool mipmapped = false, ImageOrigin origin = ImageOrigin::TopLeft,
      std::shared_ptr<ColorSpace> colorSpace = nullptr, BackingFit backingFit = BackingFit::Exact,
      uint32_t renderFlags = 0);

  /*
   * Purges all unreferenced proxies.
   */
  void purgeExpiredProxies();

  /**
   * Flushes the pending shared vertex buffer to upload the vertices to the GPU.
   */
  void flushSharedVertexBuffer();

  /**
   * Clears the block buffer used for shared vertex buffer.
   */
  void clearSharedVertexBuffer();

  /**
   * Stores the given proxy in the map with the new uniqueKey.
   */
  void assignProxyUniqueKey(std::shared_ptr<ResourceProxy> proxy, const UniqueKey& uniqueKey);

 private:
  Context* context = nullptr;
  ResourceKeyMap<std::weak_ptr<ResourceProxy>> proxyMap = {};
  bool sharedVertexBufferFlushed = false;
  std::shared_ptr<GPUBufferProxy> sharedVertexBuffer = nullptr;
  std::vector<std::shared_ptr<Task>> sharedVertexBufferTasks = {};
  BlockBuffer vertexBlockBuffer = {};
  SlidingWindowTracker maxValueTracker = {10};

  std::shared_ptr<GPUBufferProxy> findOrWrapGPUBufferProxy(const UniqueKey& uniqueKey);

  void addResourceProxy(std::shared_ptr<ResourceProxy> proxy, const UniqueKey& uniqueKey = {});

  void uploadSharedVertexBuffer(std::shared_ptr<Data> data);

  std::shared_ptr<TextureProxy> createTextureProxyByImageSource(
      std::shared_ptr<DataSource<ImageBuffer>> source, int width, int height, bool alphaOnly,
      bool mipmapped = false);
};
}  // namespace tgfx
