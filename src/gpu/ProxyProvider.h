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

#include "gpu/proxies/RenderTargetProxy.h"
#include "gpu/proxies/TextureProxy.h"
#include "images/ImageGeneratorTask.h"
#include "tgfx/core/ImageBuffer.h"

namespace tgfx {
/**
 * A factory for creating proxy-derived objects.
 */
class ProxyProvider {
 public:
  explicit ProxyProvider(Context* context);

  /*
   * Finds a texture proxy by the specified UniqueKey.
   */
  std::shared_ptr<TextureProxy> findTextureProxy(const UniqueKey& uniqueKey);

  /*
   * Create a texture proxy for the image buffer. The image buffer will be released after being
   * uploaded to the GPU.
   */
  std::shared_ptr<TextureProxy> createTextureProxy(std::shared_ptr<ImageBuffer> imageBuffer,
                                                   bool mipMapped = false);

  /*
   * Create a texture proxy for the ImageGeneratorTask. The task will be released after the
   * associated texture is instantiated.
   */
  std::shared_ptr<TextureProxy> createTextureProxy(std::shared_ptr<ImageGenerator> generator,
                                                   bool mipMapped = false,
                                                   bool disableAsyncTask = false);

  /*
   * Create a texture proxy for the ImageGeneratorTask. The task will be released after the
   * associated texture is instantiated.
   */
  std::shared_ptr<TextureProxy> createTextureProxy(std::shared_ptr<ImageGeneratorTask> task,
                                                   bool mipMapped = false);

  /**
   * Create a TextureProxy without any pixel data.
   */
  std::shared_ptr<TextureProxy> createTextureProxy(int width, int height, PixelFormat format,
                                                   bool mipMapped = false,
                                                   ImageOrigin origin = ImageOrigin::TopLeft);

  /*
   * Create a texture proxy that wraps an existing texture.
   */
  std::shared_ptr<TextureProxy> wrapTexture(std::shared_ptr<Texture> texture);

 private:
  Context* context = nullptr;
  std::unordered_map<uint32_t, TextureProxy*> proxyOwnerMap = {};

  void changeUniqueKey(TextureProxy* proxy, const UniqueKey& uniqueKey);

  void removeUniqueKey(TextureProxy* proxy);

  friend class TextureProxy;
  friend class RenderTargetProxy;
};
}  // namespace tgfx
