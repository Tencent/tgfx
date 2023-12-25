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

#include "ProxyProvider.h"
#include "gpu/PlainTexture.h"
#include "gpu/proxies/DeferredTextureProxy.h"
#include "gpu/proxies/ImageBufferTextureProxy.h"
#include "gpu/proxies/ImageGeneratorTextureProxy.h"
#include "gpu/proxies/TextureRenderTargetProxy.h"

namespace tgfx {
ProxyProvider::ProxyProvider(Context* context) : context(context) {
}

std::shared_ptr<TextureProxy> ProxyProvider::findTextureProxy(const UniqueKey& uniqueKey) {
  if (uniqueKey.empty()) {
    return nullptr;
  }
  auto result = proxyOwnerMap.find(uniqueKey.uniqueID());
  if (result != proxyOwnerMap.end()) {
    return result->second->weakThis.lock();
  }
  auto resourceCache = context->resourceCache();
  auto texture = std::static_pointer_cast<Texture>(resourceCache->findUniqueResource(uniqueKey));
  if (texture == nullptr) {
    return nullptr;
  }
  auto proxy = wrapTexture(texture);
  proxy->assignUniqueKey(uniqueKey, false);
  return proxy;
}

std::shared_ptr<TextureProxy> ProxyProvider::createTextureProxy(
    std::shared_ptr<ImageBuffer> imageBuffer, bool mipMapped) {
  if (imageBuffer == nullptr) {
    return nullptr;
  }
  auto proxy = std::shared_ptr<ImageBufferTextureProxy>(
      new ImageBufferTextureProxy(this, std::move(imageBuffer), mipMapped));
  proxy->weakThis = proxy;
  return proxy;
}

std::shared_ptr<TextureProxy> ProxyProvider::createTextureProxy(
    std::shared_ptr<ImageGenerator> generator, bool mipMapped, bool disableAsyncTask) {
  if (generator == nullptr) {
    return nullptr;
  }
  if (disableAsyncTask || generator->asyncSupport()) {
    auto buffer = generator->makeBuffer(!mipMapped);
    return createTextureProxy(std::move(buffer), mipMapped);
  }
  auto task = ImageGeneratorTask::MakeFrom(std::move(generator), !mipMapped);
  return createTextureProxy(std::move(task), mipMapped);
}

std::shared_ptr<TextureProxy> ProxyProvider::createTextureProxy(
    std::shared_ptr<ImageGeneratorTask> task, bool mipMapped) {
  if (task == nullptr) {
    return nullptr;
  }
  auto proxy = std::shared_ptr<ImageGeneratorTextureProxy>(
      new ImageGeneratorTextureProxy(this, std::move(task), mipMapped));
  proxy->weakThis = proxy;
  return proxy;
}

std::shared_ptr<TextureProxy> ProxyProvider::createTextureProxy(int width, int height,
                                                                PixelFormat format, bool mipMapped,
                                                                ImageOrigin origin) {
  if (!PlainTexture::CheckSizeAndFormat(context, width, height, format)) {
    return nullptr;
  }
  auto proxy = std::shared_ptr<DeferredTextureProxy>(
      new DeferredTextureProxy(this, width, height, format, mipMapped, origin));
  proxy->weakThis = proxy;
  return proxy;
}

std::shared_ptr<TextureProxy> ProxyProvider::wrapTexture(std::shared_ptr<Texture> texture) {
  if (texture == nullptr || texture->getContext() != context) {
    return nullptr;
  }
  auto proxy = std::shared_ptr<TextureProxy>(new TextureProxy(this, std::move(texture)));
  proxy->weakThis = proxy;
  return proxy;
}

void ProxyProvider::changeUniqueKey(TextureProxy* proxy, const UniqueKey& uniqueKey) {
  auto result = proxyOwnerMap.find(uniqueKey.uniqueID());
  if (result != proxyOwnerMap.end()) {
    result->second->removeUniqueKey();
  }
  if (!proxy->uniqueKey.empty()) {
    proxyOwnerMap.erase(proxy->uniqueKey.uniqueID());
  }
  proxy->uniqueKey = uniqueKey;
  proxyOwnerMap[uniqueKey.uniqueID()] = proxy;
}

void ProxyProvider::removeUniqueKey(TextureProxy* proxy) {
  proxyOwnerMap.erase(proxy->uniqueKey.uniqueID());
  proxy->uniqueKey = {};
}

}  // namespace tgfx
