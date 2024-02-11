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

std::shared_ptr<GpuBufferProxy> ProxyProvider::createGpuBufferProxy(const ResourceKey& resourceKey,
                                                                    std::shared_ptr<Data> data,
                                                                    BufferType bufferType,
                                                                    uint32_t renderFlags) {
  if (data == nullptr || data->empty()) {
    return nullptr;
  }
  auto provider = std::make_shared<DataWrapper>(std::move(data));
  renderFlags |= RenderFlags::DisableAsyncTask;
  return createGpuBufferProxy(resourceKey, std::move(provider), bufferType, renderFlags);
}

std::shared_ptr<GpuBufferProxy> ProxyProvider::createGpuBufferProxy(
    const ResourceKey& resourceKey, std::shared_ptr<DataProvider> provider, BufferType bufferType,
    uint32_t renderFlags) {
  auto proxy = findGpuBufferProxy(resourceKey);
  if (proxy != nullptr) {
    return proxy;
  }
  auto proxyKey = GetProxyKey(resourceKey, renderFlags);
  auto async = !(renderFlags & RenderFlags::DisableAsyncTask);
  auto task = GpuBufferCreateTask::MakeFrom(proxyKey, bufferType, std::move(provider), async);
  if (task == nullptr) {
    return nullptr;
  }
  context->drawingManager()->addResourceTask(std::move(task));
  proxy = std::shared_ptr<GpuBufferProxy>(new GpuBufferProxy(proxyKey, bufferType));
  addResourceProxy(proxy, resourceKey.domain());
  return proxy;
}

std::shared_ptr<TextureProxy> ProxyProvider::createTextureProxy(
    const ResourceKey& resourceKey, std::shared_ptr<ImageBuffer> imageBuffer, bool mipmapped,
    uint32_t renderFlags) {
  auto decoder = ImageDecoder::Wrap(std::move(imageBuffer));
  return createTextureProxy(resourceKey, std::move(decoder), mipmapped, renderFlags);
}

std::shared_ptr<TextureProxy> ProxyProvider::createTextureProxy(
    const ResourceKey& resourceKey, std::shared_ptr<ImageGenerator> generator, bool mipmapped,
    uint32_t renderFlags) {
  auto asyncDecoding = !(renderFlags & RenderFlags::DisableAsyncTask);
  auto decoder = ImageDecoder::MakeFrom(std::move(generator), !mipmapped, asyncDecoding);
  return createTextureProxy(resourceKey, std::move(decoder), mipmapped, renderFlags);
}

std::shared_ptr<TextureProxy> ProxyProvider::createTextureProxy(
    const ResourceKey& resourceKey, std::shared_ptr<ImageDecoder> decoder, bool mipmapped,
    uint32_t renderFlags) {
  auto proxy = findTextureProxy(resourceKey);
  if (proxy != nullptr) {
    return proxy;
  }
  auto proxyKey = GetProxyKey(resourceKey, renderFlags);
  auto task = TextureCreateTask::MakeFrom(proxyKey, decoder, mipmapped);
  if (task == nullptr) {
    return nullptr;
  }
  context->drawingManager()->addResourceTask(std::move(task));
  proxy = std::shared_ptr<TextureProxy>(new TextureProxy(
      proxyKey, decoder->width(), decoder->height(), mipmapped, decoder->isAlphaOnly()));
  addResourceProxy(proxy, resourceKey.domain());
  return proxy;
}

std::shared_ptr<TextureProxy> ProxyProvider::createTextureProxy(const ResourceKey& resourceKey,
                                                                int width, int height,
                                                                PixelFormat format, bool mipmapped,
                                                                ImageOrigin origin,
                                                                uint32_t renderFlags) {
  auto proxy = findTextureProxy(resourceKey);
  if (proxy != nullptr) {
    return proxy;
  }
  if (!PlainTexture::CheckSizeAndFormat(context, width, height, format)) {
    return nullptr;
  }
  auto proxyKey = GetProxyKey(resourceKey, renderFlags);
  auto task = TextureCreateTask::MakeFrom(proxyKey, width, height, format, mipmapped, origin);
  if (task == nullptr) {
    return nullptr;
  }
  context->drawingManager()->addResourceTask(std::move(task));
  auto isAlphaOnly = format == PixelFormat::ALPHA_8;
  proxy = std::shared_ptr<TextureProxy>(
      new TextureProxy(proxyKey, width, height, mipmapped, isAlphaOnly, origin));
  addResourceProxy(proxy, resourceKey.domain());
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
  auto resourceKey = ResourceKey::Make();
  texture->assignResourceKey(resourceKey);
  auto proxy = std::shared_ptr<TextureProxy>(
      new TextureProxy(resourceKey, texture->width(), texture->height(), texture->hasMipmaps(),
                       texture->isAlphaOnly(), texture->origin(), !adopted));
  addResourceProxy(proxy, resourceKey.domain());
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
  auto resourceKey = ResourceKey::Make();
  auto task = RenderTargetCreateTask::MakeFrom(resourceKey, textureProxy->getResourceKey(), format,
                                               sampleCount);
  if (task == nullptr) {
    return nullptr;
  }
  context->drawingManager()->addResourceTask(std::move(task));
  auto proxy = std::shared_ptr<RenderTargetProxy>(
      new TextureRenderTargetProxy(resourceKey, std::move(textureProxy), format, sampleCount));
  addResourceProxy(proxy, resourceKey.domain());
  return proxy;
}

std::shared_ptr<RenderTargetProxy> ProxyProvider::wrapBackendRenderTarget(
    const BackendRenderTarget& backendRenderTarget, ImageOrigin origin) {
  auto renderTarget = RenderTarget::MakeFrom(context, backendRenderTarget, origin);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  auto resourceKey = ResourceKey::Make();
  renderTarget->assignResourceKey(resourceKey);
  auto proxy = std::shared_ptr<RenderTargetProxy>(new RenderTargetProxy(
      resourceKey, renderTarget->width(), renderTarget->height(), renderTarget->format(),
      renderTarget->sampleCount(), renderTarget->origin()));
  addResourceProxy(proxy, resourceKey.domain());
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

ResourceKey ProxyProvider::GetProxyKey(const ResourceKey& resourceKey, uint32_t renderFlags) {
  if (resourceKey.empty() || renderFlags & RenderFlags::DisableCache) {
    // Disable cache, generate a temporary ResourceKey exclusively for proxy usage.
    return ResourceKey::Make();
  }
  return resourceKey;
}

std::shared_ptr<GpuBufferProxy> ProxyProvider::findGpuBufferProxy(const ResourceKey& resourceKey) {
  auto proxy = std::static_pointer_cast<GpuBufferProxy>(findProxy(resourceKey));
  if (proxy != nullptr) {
    return proxy;
  }
  auto gpuBuffer = Resource::Get<GpuBuffer>(context, resourceKey);
  if (gpuBuffer == nullptr) {
    return nullptr;
  }
  proxy = std::shared_ptr<GpuBufferProxy>(new GpuBufferProxy(resourceKey, gpuBuffer->bufferType()));
  addResourceProxy(proxy, resourceKey.domain());
  return proxy;
}

std::shared_ptr<TextureProxy> ProxyProvider::findTextureProxy(const ResourceKey& resourceKey) {
  auto proxy = std::static_pointer_cast<TextureProxy>(findProxy(resourceKey));
  if (proxy != nullptr) {
    return proxy;
  }
  auto texture = Resource::Get<Texture>(context, resourceKey);
  if (texture == nullptr) {
    return nullptr;
  }
  proxy = std::shared_ptr<TextureProxy>(
      new TextureProxy(resourceKey, texture->width(), texture->height(), texture->hasMipmaps(),
                       texture->isAlphaOnly(), texture->origin()));
  addResourceProxy(proxy, resourceKey.domain());
  return proxy;
}

std::shared_ptr<ResourceProxy> ProxyProvider::findProxy(const ResourceKey& resourceKey) {
  if (resourceKey.empty()) {
    return nullptr;
  }
  auto result = proxyMap.find(resourceKey.domain());
  if (result != proxyMap.end()) {
    auto proxy = result->second.lock();
    if (proxy != nullptr) {
      return proxy;
    }
    proxyMap.erase(result);
  }
  return nullptr;
}

void ProxyProvider::addResourceProxy(std::shared_ptr<ResourceProxy> proxy, uint32_t domainID) {
  if (domainID == 0) {
    domainID = proxy->handle.domain();
  }
  proxy->context = context;
  proxyMap[domainID] = proxy;
}

}  // namespace tgfx
