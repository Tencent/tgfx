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
#include "gpu/DrawingManager.h"
#include "gpu/PlainTexture.h"
#include "gpu/proxies/TextureRenderTargetProxy.h"
#include "gpu/tasks/GpuBufferCreateTask.h"
#include "gpu/tasks/RenderTargetCreateTask.h"
#include "gpu/tasks/TextureCreateTask.h"

namespace tgfx {
ProxyProvider::ProxyProvider(Context* context) : context(context) {
}

bool ProxyProvider::hasResourceProxy(const UniqueKey& uniqueKey) {
  auto proxy = findResourceProxy(uniqueKey);
  if (proxy == nullptr) {
    return false;
  }
  return proxy->uniqueKey.domainID() == uniqueKey.domainID();
}

class DataWrapper : public DataProvider {
 public:
  DataWrapper(std::shared_ptr<Data> data) : data(std::move(data)) {
  }

  std::shared_ptr<Data> getData() const override {
    return data;
  }

 private:
  std::shared_ptr<Data> data = nullptr;
};

std::shared_ptr<GpuBufferProxy> ProxyProvider::createGpuBufferProxy(const UniqueKey& uniqueKey,
                                                                    std::shared_ptr<Data> data,
                                                                    BufferType bufferType,
                                                                    uint32_t renderFlags) {
  if (data == nullptr || data->empty()) {
    return nullptr;
  }
  auto provider = std::make_shared<DataWrapper>(std::move(data));
  renderFlags |= RenderFlags::DisableAsyncTask;
  return createGpuBufferProxy(uniqueKey, std::move(provider), bufferType, renderFlags);
}

std::shared_ptr<GpuBufferProxy> ProxyProvider::createGpuBufferProxy(
    const UniqueKey& uniqueKey, std::shared_ptr<DataProvider> provider, BufferType bufferType,
    uint32_t renderFlags) {
  auto proxy = findGpuBufferProxy(uniqueKey);
  if (proxy != nullptr) {
    return proxy;
  }
  auto strongKey = GetStrongKey(uniqueKey, renderFlags);
  auto async = !(renderFlags & RenderFlags::DisableAsyncTask);
  auto task = GpuBufferCreateTask::MakeFrom(strongKey, bufferType, std::move(provider), async);
  if (task == nullptr) {
    return nullptr;
  }
  context->drawingManager()->addResourceTask(std::move(task));
  proxy = std::shared_ptr<GpuBufferProxy>(new GpuBufferProxy(bufferType));
  addResourceProxy(proxy, strongKey, uniqueKey.domainID());
  return proxy;
}

std::shared_ptr<TextureProxy> ProxyProvider::createTextureProxy(
    const UniqueKey& uniqueKey, std::shared_ptr<ImageBuffer> imageBuffer, bool mipMapped,
    uint32_t renderFlags) {
  auto decoder = ImageDecoder::Wrap(std::move(imageBuffer));
  return createTextureProxy(uniqueKey, std::move(decoder), mipMapped, renderFlags);
}

std::shared_ptr<TextureProxy> ProxyProvider::createTextureProxy(
    const UniqueKey& uniqueKey, std::shared_ptr<ImageGenerator> generator, bool mipMapped,
    uint32_t renderFlags) {
  auto asyncDecoding = !(renderFlags & RenderFlags::DisableAsyncTask);
  auto decoder = ImageDecoder::MakeFrom(std::move(generator), !mipMapped, asyncDecoding);
  return createTextureProxy(uniqueKey, std::move(decoder), mipMapped, renderFlags);
}

std::shared_ptr<TextureProxy> ProxyProvider::createTextureProxy(
    const UniqueKey& uniqueKey, std::shared_ptr<ImageDecoder> decoder, bool mipMapped,
    uint32_t renderFlags) {
  auto proxy = findTextureProxy(uniqueKey);
  if (proxy != nullptr) {
    return proxy;
  }
  auto strongKey = GetStrongKey(uniqueKey, renderFlags);
  auto task = TextureCreateTask::MakeFrom(strongKey, decoder, mipMapped);
  if (task == nullptr) {
    return nullptr;
  }
  context->drawingManager()->addResourceTask(std::move(task));
  proxy = std::shared_ptr<TextureProxy>(
      new TextureProxy(decoder->width(), decoder->height(), mipMapped, decoder->isAlphaOnly()));
  addResourceProxy(proxy, strongKey, uniqueKey.domainID());
  return proxy;
}

std::shared_ptr<TextureProxy> ProxyProvider::createTextureProxy(int width, int height,
                                                                PixelFormat format, bool mipMapped,
                                                                ImageOrigin origin) {
  if (!PlainTexture::CheckSizeAndFormat(context, width, height, format)) {
    return nullptr;
  }
  auto strongKey = UniqueKey::MakeStrong();
  auto task = TextureCreateTask::MakeFrom(strongKey, width, height, format, mipMapped, origin);
  if (task == nullptr) {
    return nullptr;
  }
  context->drawingManager()->addResourceTask(std::move(task));
  auto isAlphaOnly = format == PixelFormat::ALPHA_8;
  auto proxy = std::shared_ptr<TextureProxy>(
      new TextureProxy(width, height, mipMapped, isAlphaOnly, origin));
  addResourceProxy(proxy, strongKey, strongKey.domainID());
  return proxy;
}

std::shared_ptr<TextureProxy> ProxyProvider::wrapBackendTexture(
    const BackendTexture& backendTexture, ImageOrigin origin, bool adopted) {
  std::shared_ptr<Texture> texture = nullptr;
  if (adopted) {
    texture = Texture::MakeAdopted(context, backendTexture, origin);
  } else {
    texture = Texture::MakeFrom(context, backendTexture, origin);
  }
  if (texture == nullptr) {
    return nullptr;
  }
  auto strongKey = UniqueKey::MakeStrong();
  texture->assignUniqueKey(strongKey);
  auto proxy = std::shared_ptr<TextureProxy>(
      new TextureProxy(texture->width(), texture->height(), texture->hasMipmaps(),
                       texture->isAlphaOnly(), texture->origin(), !adopted));
  addResourceProxy(proxy, strongKey, strongKey.domainID());
  return proxy;
}

std::shared_ptr<RenderTargetProxy> ProxyProvider::createRenderTargetProxy(
    std::shared_ptr<TextureProxy> textureProxy, PixelFormat format, int sampleCount) {
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto caps = context->caps();
  if (!caps->isFormatRenderable(format)) {
    return nullptr;
  }
  sampleCount = caps->getSampleCount(sampleCount, format);
  auto strongKey = UniqueKey::MakeStrong();
  auto task =
      RenderTargetCreateTask::MakeFrom(strongKey, textureProxy->uniqueKey, format, sampleCount);
  if (task == nullptr) {
    return nullptr;
  }
  context->drawingManager()->addResourceTask(std::move(task));
  auto proxy = std::shared_ptr<RenderTargetProxy>(
      new TextureRenderTargetProxy(std::move(textureProxy), format, sampleCount));
  addResourceProxy(proxy, strongKey, strongKey.domainID());
  return proxy;
}

std::shared_ptr<RenderTargetProxy> ProxyProvider::wrapBackendRenderTarget(
    const BackendRenderTarget& backendRenderTarget, ImageOrigin origin) {
  auto renderTarget = RenderTarget::MakeFrom(context, backendRenderTarget, origin);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  auto strongKey = UniqueKey::MakeStrong();
  renderTarget->assignUniqueKey(strongKey);
  auto proxy = std::shared_ptr<RenderTargetProxy>(
      new RenderTargetProxy(renderTarget->width(), renderTarget->height(), renderTarget->format(),
                            renderTarget->sampleCount(), renderTarget->origin()));
  addResourceProxy(proxy, strongKey, strongKey.domainID());
  return proxy;
}

void ProxyProvider::purgeExpiredProxies() {
  std::vector<uint32_t> keys;
  for (auto& pair : proxyMap) {
    if (pair.second.expired()) {
      keys.push_back(pair.first);
    }
  }
  for (auto key : keys) {
    proxyMap.erase(key);
  }
}

UniqueKey ProxyProvider::GetStrongKey(const UniqueKey& uniqueKey, uint32_t renderFlags) {
  if (uniqueKey.empty() || renderFlags & RenderFlags::DisableCache) {
    // Disable cache, generate a temporary unique key exclusively for proxy usage.
    return UniqueKey::MakeStrong();
  }
  return uniqueKey.makeStrong();
}

std::shared_ptr<GpuBufferProxy> ProxyProvider::findGpuBufferProxy(const UniqueKey& uniqueKey) {
  auto proxy = std::static_pointer_cast<GpuBufferProxy>(findResourceProxy(uniqueKey));
  if (proxy != nullptr) {
    return proxy;
  }
  auto gpuBuffer = Resource::Get<GpuBuffer>(context, uniqueKey);
  if (gpuBuffer == nullptr) {
    return nullptr;
  }
  proxy = std::shared_ptr<GpuBufferProxy>(new GpuBufferProxy(gpuBuffer->bufferType()));
  addResourceProxy(proxy, uniqueKey.makeStrong(), uniqueKey.domainID());
  return proxy;
}

std::shared_ptr<TextureProxy> ProxyProvider::findTextureProxy(const UniqueKey& uniqueKey) {
  auto proxy = std::static_pointer_cast<TextureProxy>(findResourceProxy(uniqueKey));
  if (proxy != nullptr) {
    return proxy;
  }
  auto texture = Resource::Get<Texture>(context, uniqueKey);
  if (texture == nullptr) {
    return nullptr;
  }
  proxy = std::shared_ptr<TextureProxy>(
      new TextureProxy(texture->width(), texture->height(), texture->hasMipmaps(),
                       texture->isAlphaOnly(), texture->origin()));
  addResourceProxy(proxy, uniqueKey.makeStrong(), uniqueKey.domainID());
  return proxy;
}

std::shared_ptr<ResourceProxy> ProxyProvider::findResourceProxy(const UniqueKey& uniqueKey) {
  if (uniqueKey.empty()) {
    return nullptr;
  }
  auto result = proxyMap.find(uniqueKey.domainID());
  if (result != proxyMap.end()) {
    auto proxy = result->second.lock();
    if (proxy != nullptr) {
      return proxy;
    }
    proxyMap.erase(result);
  }
  return nullptr;
}

void ProxyProvider::addResourceProxy(std::shared_ptr<ResourceProxy> proxy, UniqueKey strongKey,
                                     uint32_t domainID) {
  if (domainID == 0) {
    domainID = strongKey.domainID();
  }
  proxy->context = context;
  proxy->uniqueKey = std::move(strongKey);
  proxyMap[domainID] = proxy;
}

}  // namespace tgfx
