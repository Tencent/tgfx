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
#include "core/ShapeRasterizer.h"
#include "core/shapes/MatrixShape.h"
#include "core/utils/DataTask.h"
#include "core/utils/Profiling.h"
#include "core/utils/UniqueID.h"
#include "gpu/DrawingManager.h"
#include "gpu/PlainTexture.h"
#include "gpu/proxies/DefaultTextureProxy.h"
#include "gpu/proxies/FlattenTextureProxy.h"
#include "gpu/proxies/TextureRenderTargetProxy.h"
#include "gpu/tasks/GpuBufferUploadTask.h"
#include "gpu/tasks/RenderTargetCreateTask.h"
#include "gpu/tasks/ShapeBufferUploadTask.h"
#include "gpu/tasks/TextureCreateTask.h"
#include "gpu/tasks/TextureFlattenTask.h"
#include "gpu/tasks/TextureUploadTask.h"

namespace tgfx {
ProxyProvider::ProxyProvider(Context* context) : context(context) {
}

std::shared_ptr<GpuBufferProxy> ProxyProvider::createGpuBufferProxy(const UniqueKey& uniqueKey,
                                                                    std::shared_ptr<Data> data,
                                                                    BufferType bufferType,
                                                                    uint32_t renderFlags) {
  if (data == nullptr || data->empty()) {
    return nullptr;
  }
  auto provider = DataProvider::Wrap(std::move(data));
  renderFlags |= RenderFlags::DisableAsyncTask;
  return createGpuBufferProxy(uniqueKey, std::move(provider), bufferType, renderFlags);
}

class AsyncDataProvider : public DataProvider {
 public:
  explicit AsyncDataProvider(std::shared_ptr<DataProvider> provider) {
    task = DataTask<Data>::Run([provider = std::move(provider)]() { return provider->getData(); });
  }

  std::shared_ptr<Data> getData() const override {
    return task->wait();
  }

 std::string getName() const override {
   return "AsyncDataProvider";
 }

 private:
  std::shared_ptr<DataTask<Data>> task = nullptr;
};

std::shared_ptr<GpuBufferProxy> ProxyProvider::createGpuBufferProxy(
    const UniqueKey& uniqueKey, std::shared_ptr<DataProvider> provider, BufferType bufferType,
    uint32_t renderFlags) {
  auto proxy = findOrWrapGpuBufferProxy(uniqueKey);
  if (proxy != nullptr) {
    return proxy;
  }
  if (provider == nullptr) {
    return nullptr;
  }
  if (!(renderFlags & RenderFlags::DisableAsyncTask)) {
    if (provider->getName() == "PatternedIndexBufferProvider-1" || provider->getName() == "RectCoverageVerticesProvider-1"
        || provider->getName() == "RectNonCoverageVerticesProvider-1") {
      provider = std::make_shared<AsyncDataProvider>(std::move(provider));
    }
  }
  auto proxyKey = GetProxyKey(uniqueKey, renderFlags);
  auto task = GpuBufferUploadTask::MakeFrom(proxyKey, bufferType, std::move(provider));
  if (task == nullptr) {
    return nullptr;
  }
  context->drawingManager()->addResourceTask(std::move(task));
  proxy = std::shared_ptr<GpuBufferProxy>(new GpuBufferProxy(proxyKey, bufferType));
  addResourceProxy(proxy, uniqueKey);
  return proxy;
}

class ShapeRasterizerWrapper : public ShapeBufferProvider {
 public:
  explicit ShapeRasterizerWrapper(std::shared_ptr<ShapeRasterizer> rasterizer)
      : rasterizer(std::move(rasterizer)) {
  }

  std::shared_ptr<ShapeBuffer> getBuffer() const override {
    return rasterizer->makeRasterized();
  }

 private:
  std::shared_ptr<ShapeRasterizer> rasterizer = nullptr;
};

class AsyncShapeBufferProvider : public ShapeBufferProvider {
 public:
  explicit AsyncShapeBufferProvider(std::shared_ptr<ShapeRasterizer> rasterizer) {
    task = DataTask<ShapeBuffer>::Run(
        [rasterizer = std::move(rasterizer)]() { return rasterizer->makeRasterized(); });
  }

  std::shared_ptr<ShapeBuffer> getBuffer() const override {
    return task->wait();
  }

 private:
  std::shared_ptr<DataTask<ShapeBuffer>> task = nullptr;
};

static UniqueKey AppendClipBoundsKey(const UniqueKey& uniqueKey, const Rect& clipBounds) {
  static const auto ClipBoundsType = UniqueID::Next();
  BytesKey bytesKey(5);
  bytesKey.write(ClipBoundsType);
  bytesKey.write(clipBounds.left);
  bytesKey.write(clipBounds.top);
  bytesKey.write(clipBounds.right);
  bytesKey.write(clipBounds.bottom);
  return UniqueKey::Append(uniqueKey, bytesKey.data(), bytesKey.size());
}

std::shared_ptr<GpuShapeProxy> ProxyProvider::createGpuShapeProxy(std::shared_ptr<Shape> shape,
                                                                  bool antiAlias,
                                                                  const Rect& clipBounds,
                                                                  uint32_t renderFlags) {
  if (shape == nullptr) {
    return nullptr;
  }
  auto drawingMatrix = Matrix::I();
  auto isInverseFillType = shape->isInverseFillType();
  if (shape->type() == Shape::Type::Matrix && !isInverseFillType) {
    auto matrixShape = std::static_pointer_cast<MatrixShape>(shape);
    auto scales = matrixShape->matrix.getAxisScales();
    if (scales.x == scales.y) {
      DEBUG_ASSERT(scales.x != 0);
      drawingMatrix = matrixShape->matrix;
      drawingMatrix.preScale(1.0f / scales.x, 1.0f / scales.x);
      shape = Shape::ApplyMatrix(matrixShape->shape, Matrix::MakeScale(scales.x));
    }
  }
  auto shapeBounds = shape->getBounds();
  auto uniqueKey = shape->getUniqueKey();
  if (isInverseFillType) {
    uniqueKey =
        AppendClipBoundsKey(uniqueKey, clipBounds.makeOffset(-shapeBounds.left, -shapeBounds.top));
  }
  if (!antiAlias) {
    static const auto NonAntialiasShapeType = UniqueID::Next();
    uniqueKey = UniqueKey::Append(uniqueKey, &NonAntialiasShapeType, 1);
  }
  auto bounds = isInverseFillType ? clipBounds : shapeBounds;
  drawingMatrix.preTranslate(bounds.x(), bounds.y());
  static const auto TriangleShapeType = UniqueID::Next();
  static const auto TextureShapeType = UniqueID::Next();
  auto triangleKey = UniqueKey::Append(uniqueKey, &TriangleShapeType, 1);
  auto triangleProxy = findOrWrapGpuBufferProxy(triangleKey);
  if (triangleProxy != nullptr) {
    return std::make_shared<GpuShapeProxy>(drawingMatrix, triangleProxy, nullptr);
  }
  auto textureKey = UniqueKey::Append(uniqueKey, &TextureShapeType, 1);
  auto textureProxy = findOrWrapTextureProxy(textureKey);
  if (textureProxy != nullptr) {
    return std::make_shared<GpuShapeProxy>(drawingMatrix, nullptr, textureProxy);
  }
  auto width = static_cast<int>(ceilf(bounds.width()));
  auto height = static_cast<int>(ceilf(bounds.height()));
  shape = Shape::ApplyMatrix(std::move(shape), Matrix::MakeTrans(-bounds.x(), -bounds.y()));
  auto rasterizer = std::make_shared<ShapeRasterizer>(width, height, std::move(shape), antiAlias);
  std::shared_ptr<ShapeBufferProvider> provider = nullptr;
  if (!(renderFlags & RenderFlags::DisableAsyncTask) && rasterizer->asyncSupport()) {
    provider = std::make_shared<AsyncShapeBufferProvider>(std::move(rasterizer));
  } else {
    provider = std::make_shared<ShapeRasterizerWrapper>(std::move(rasterizer));
  }
  auto triangleProxyKey = GetProxyKey(triangleKey, renderFlags);
  auto textureProxyKey = GetProxyKey(textureKey, renderFlags);
  auto task =
      ShapeBufferUploadTask::MakeFrom(triangleProxyKey, textureProxyKey, std::move(provider));
  if (task == nullptr) {
    return nullptr;
  }
  context->drawingManager()->addResourceTask(std::move(task));
  triangleProxy =
      std::shared_ptr<GpuBufferProxy>(new GpuBufferProxy(triangleProxyKey, BufferType::Vertex));
  addResourceProxy(triangleProxy, triangleKey);
  textureProxy = std::shared_ptr<TextureProxy>(
      new DefaultTextureProxy(textureProxyKey, width, height, false, true));
  addResourceProxy(textureProxy, textureKey);
  return std::make_shared<GpuShapeProxy>(drawingMatrix, triangleProxy, textureProxy);
}

std::shared_ptr<TextureProxy> ProxyProvider::createTextureProxy(
    const UniqueKey& uniqueKey, std::shared_ptr<ImageBuffer> imageBuffer, bool mipmapped,
    uint32_t renderFlags) {
  auto decoder = ImageDecoder::Wrap(std::move(imageBuffer));
  return createTextureProxy(uniqueKey, std::move(decoder), mipmapped, renderFlags);
}

std::shared_ptr<TextureProxy> ProxyProvider::createTextureProxy(
    const UniqueKey& uniqueKey, std::shared_ptr<ImageGenerator> generator, bool mipmapped,
    uint32_t renderFlags) {
  auto proxy = findOrWrapTextureProxy(uniqueKey);
  if (proxy != nullptr) {
    return proxy;
  }
  auto asyncDecoding = !(renderFlags & RenderFlags::DisableAsyncTask);
  auto decoder = ImageDecoder::MakeFrom(std::move(generator), !mipmapped, asyncDecoding);
  return doCreateTextureProxy(uniqueKey, std::move(decoder), mipmapped, renderFlags);
}

std::shared_ptr<TextureProxy> ProxyProvider::createTextureProxy(
    const UniqueKey& uniqueKey, std::shared_ptr<ImageDecoder> decoder, bool mipmapped,
    uint32_t renderFlags) {
  auto proxy = findOrWrapTextureProxy(uniqueKey);
  if (proxy != nullptr) {
    return proxy;
  }
  return doCreateTextureProxy(uniqueKey, std::move(decoder), mipmapped, renderFlags);
}

std::shared_ptr<TextureProxy> ProxyProvider::doCreateTextureProxy(
    const UniqueKey& uniqueKey, std::shared_ptr<ImageDecoder> decoder, bool mipmapped,
    uint32_t renderFlags) {
  auto proxyKey = GetProxyKey(uniqueKey, renderFlags);
  auto task = TextureUploadTask::MakeFrom(proxyKey, decoder, mipmapped);
  if (task == nullptr) {
    return nullptr;
  }
  context->drawingManager()->addResourceTask(std::move(task));
  auto proxy = std::shared_ptr<TextureProxy>(new DefaultTextureProxy(
      proxyKey, decoder->width(), decoder->height(), mipmapped, decoder->isAlphaOnly()));
  addResourceProxy(proxy, uniqueKey);
  return proxy;
}

std::shared_ptr<TextureProxy> ProxyProvider::createTextureProxy(const UniqueKey& uniqueKey,
                                                                int width, int height,
                                                                PixelFormat format, bool mipmapped,
                                                                ImageOrigin origin,
                                                                uint32_t renderFlags) {
  auto proxy = findOrWrapTextureProxy(uniqueKey);
  if (proxy != nullptr) {
    return proxy;
  }
  if (!PlainTexture::CheckSizeAndFormat(context, width, height, format)) {
    return nullptr;
  }
  auto proxyKey = GetProxyKey(uniqueKey, renderFlags);
  auto task = TextureCreateTask::MakeFrom(proxyKey, width, height, format, mipmapped, origin);
  if (task == nullptr) {
    return nullptr;
  }
  context->drawingManager()->addResourceTask(std::move(task));
  auto isAlphaOnly = format == PixelFormat::ALPHA_8;
  proxy = std::shared_ptr<TextureProxy>(
      new DefaultTextureProxy(proxyKey, width, height, mipmapped, isAlphaOnly, origin));
  addResourceProxy(proxy, uniqueKey);
  return proxy;
}

std::shared_ptr<TextureProxy> ProxyProvider::flattenTextureProxy(
    std::shared_ptr<TextureProxy> source) {
  if (source == nullptr) {
    return nullptr;
  }
  auto uniqueKey = UniqueKey::Make();
  auto task = std::shared_ptr<TextureFlattenTask>(new TextureFlattenTask(uniqueKey, source));
  context->drawingManager()->addTextureFlattenTask(std::move(task));
  auto proxy = std::shared_ptr<TextureProxy>(new FlattenTextureProxy(uniqueKey, std::move(source)));
  addResourceProxy(proxy, uniqueKey);
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
  auto uniqueKey = UniqueKey::Make();
  texture->assignUniqueKey(uniqueKey);
  auto proxy = std::shared_ptr<TextureProxy>(
      new DefaultTextureProxy(uniqueKey, texture->width(), texture->height(), texture->hasMipmaps(),
                              texture->isAlphaOnly(), texture->origin(), !adopted));
  addResourceProxy(proxy, uniqueKey);
  return proxy;
}

std::shared_ptr<RenderTargetProxy> ProxyProvider::createRenderTargetProxy(
    std::shared_ptr<TextureProxy> textureProxy, PixelFormat format, int sampleCount,
    bool clearAll) {
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto caps = context->caps();
  if (!caps->isFormatRenderable(format)) {
    return nullptr;
  }
  sampleCount = caps->getSampleCount(sampleCount, format);
  auto uniqueKey = UniqueKey::Make();
  auto task =
      RenderTargetCreateTask::MakeFrom(uniqueKey, textureProxy, format, sampleCount, clearAll);
  if (task == nullptr) {
    return nullptr;
  }
  auto drawingManager = context->drawingManager();
  drawingManager->addResourceTask(std::move(task));
  auto proxy = std::shared_ptr<RenderTargetProxy>(
      new TextureRenderTargetProxy(uniqueKey, std::move(textureProxy), format, sampleCount));
  addResourceProxy(proxy, uniqueKey);
  return proxy;
}

std::shared_ptr<RenderTargetProxy> ProxyProvider::wrapBackendRenderTarget(
    const BackendRenderTarget& backendRenderTarget, ImageOrigin origin) {
  auto renderTarget = RenderTarget::MakeFrom(context, backendRenderTarget, origin);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  auto uniqueKey = UniqueKey::Make();
  renderTarget->assignUniqueKey(uniqueKey);
  auto proxy = std::shared_ptr<RenderTargetProxy>(new RenderTargetProxy(
      uniqueKey, renderTarget->width(), renderTarget->height(), renderTarget->format(),
      renderTarget->sampleCount(), renderTarget->origin()));
  addResourceProxy(proxy, uniqueKey);
  return proxy;
}

void ProxyProvider::purgeExpiredProxies() {
  std::vector<const ResourceKey*> keys = {};
  for (auto& pair : proxyMap) {
    if (pair.second.expired()) {
      keys.push_back(&pair.first);
    }
  }
  for (auto& key : keys) {
    proxyMap.erase(*key);
  }
}

UniqueKey ProxyProvider::GetProxyKey(const UniqueKey& uniqueKey, uint32_t renderFlags) {
  if (uniqueKey.empty() || renderFlags & RenderFlags::DisableCache) {
    // Disable cache, generate a temporary UniqueKey exclusively for proxy usage.
    return UniqueKey::Make();
  }
  return uniqueKey;
}

std::shared_ptr<GpuBufferProxy> ProxyProvider::findOrWrapGpuBufferProxy(
    const UniqueKey& uniqueKey) {
  auto proxy = std::static_pointer_cast<GpuBufferProxy>(findProxy(uniqueKey));
  if (proxy != nullptr) {
    return proxy;
  }
  auto gpuBuffer = Resource::Find<GpuBuffer>(context, uniqueKey);
  if (gpuBuffer == nullptr) {
    return nullptr;
  }
  proxy = std::shared_ptr<GpuBufferProxy>(new GpuBufferProxy(uniqueKey, gpuBuffer->bufferType()));
  addResourceProxy(proxy, uniqueKey);
  return proxy;
}

std::shared_ptr<TextureProxy> ProxyProvider::findOrWrapTextureProxy(const UniqueKey& uniqueKey) {
  auto proxy = std::static_pointer_cast<TextureProxy>(findProxy(uniqueKey));
  if (proxy != nullptr) {
    return proxy;
  }
  auto texture = Resource::Find<Texture>(context, uniqueKey);
  if (texture == nullptr) {
    return nullptr;
  }
  proxy = std::shared_ptr<TextureProxy>(
      new DefaultTextureProxy(uniqueKey, texture->width(), texture->height(), texture->hasMipmaps(),
                              texture->isAlphaOnly(), texture->origin()));
  addResourceProxy(proxy, uniqueKey);
  return proxy;
}

std::shared_ptr<ResourceProxy> ProxyProvider::findProxy(const UniqueKey& uniqueKey) {
  if (uniqueKey.empty()) {
    return nullptr;
  }
  auto result = proxyMap.find(uniqueKey);
  if (result != proxyMap.end()) {
    auto proxy = result->second.lock();
    if (proxy != nullptr) {
      return proxy;
    }
    proxyMap.erase(result);
  }
  return nullptr;
}

void ProxyProvider::addResourceProxy(std::shared_ptr<ResourceProxy> proxy,
                                     const UniqueKey& uniqueKey) {
  proxy->context = context;
  if (uniqueKey.empty()) {
    proxyMap[proxy->getUniqueKey()] = proxy;
  } else {
    proxyMap[uniqueKey] = proxy;
  }
}

}  // namespace tgfx
