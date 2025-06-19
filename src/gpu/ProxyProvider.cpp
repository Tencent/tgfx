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
#include <memory>
#include "core/DataSource.h"
#include "core/ImageSource.h"
#include "core/ShapeRasterizer.h"
#include "core/shapes/MatrixShape.h"
#include "core/utils/USE.h"
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
#include "tgfx/core/DataView.h"
#include "tgfx/core/ImageBuffer.h"
#include "tgfx/core/RenderFlags.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {
ProxyProvider::ProxyProvider(Context* context) : context(context), blockBuffer(1 << 14) {  // 16KB
}

std::shared_ptr<GpuBufferProxy> ProxyProvider::createGpuBufferProxy(const UniqueKey& uniqueKey,
                                                                    std::shared_ptr<Data> data,
                                                                    BufferType bufferType,
                                                                    uint32_t renderFlags) {
  if (data == nullptr || data->empty()) {
    return nullptr;
  }
  auto source = DataSource<Data>::Wrap(std::move(data));
  renderFlags |= RenderFlags::DisableAsyncTask;
  return createGpuBufferProxy(uniqueKey, std::move(source), bufferType, renderFlags);
}

std::shared_ptr<GpuBufferProxy> ProxyProvider::createGpuBufferProxy(
    const UniqueKey& uniqueKey, std::unique_ptr<DataSource<Data>> source, BufferType bufferType,
    uint32_t renderFlags) {
  if (source == nullptr) {
    return nullptr;
  }
  auto proxy = findOrWrapGpuBufferProxy(uniqueKey);
  if (proxy != nullptr) {
    return proxy;
  }
  std::shared_ptr<DataSource<Data>> dataSource = nullptr;
#ifdef TGFX_USE_THREADS
  if (!(renderFlags & RenderFlags::DisableAsyncTask)) {
    dataSource =
        DataSource<Data>::Async(std::move(source), context->drawingBuffer()->getReferenceCounter());
  } else {
    dataSource = std::move(source);
  }
#endif
  auto proxyKey = GetProxyKey(uniqueKey, renderFlags);
  auto task = context->drawingBuffer()->make<GpuBufferUploadTask>(proxyKey, bufferType, dataSource);
  context->drawingManager()->addResourceTask(std::move(task));
  proxy = std::shared_ptr<GpuBufferProxy>(new GpuBufferProxy(proxyKey, bufferType));
  addResourceProxy(proxy, uniqueKey);
  return proxy;
}

std::pair<std::shared_ptr<GpuBufferProxy>, size_t> ProxyProvider::createSharedVertexBuffer(
    PlacementPtr<VertexProvider> provider, uint32_t renderFlags) {
  if (provider == nullptr) {
    return {nullptr, 0};
  }
  DEBUG_ASSERT(!sharedVertexBufferFlushed);
  auto byteSize = provider->vertexCount() * sizeof(float);
  auto lastBlock = blockBuffer.currentBlock();
  auto vertices = reinterpret_cast<float*>(blockBuffer.allocate(byteSize));
  if (vertices == nullptr) {
    LOGE("ProxyProvider::createSharedVertexBuffer() Failed to allocate memory!");
    return {nullptr, 0};
  }
  auto offset = lastBlock.second;
  auto currentBlock = blockBuffer.currentBlock();
  if (lastBlock.first != nullptr && lastBlock.first != currentBlock.first) {
    DEBUG_ASSERT(sharedVertexBuffer != nullptr);
    auto data = Data::MakeWithoutCopy(lastBlock.first, lastBlock.second);
    uploadSharedVertexBuffer(std::move(data));
    offset = 0;
  }
#ifdef TGFX_USE_THREADS
  if (renderFlags & RenderFlags::DisableAsyncTask) {
    provider->getVertices(vertices);
  } else {
    auto task = std::make_shared<VertexProviderTask>(std::move(provider), vertices);
    Task::Run(task);
    sharedVertexBufferTasks.push_back(std::move(task));
  }
#else
  USE(renderFlags);
  provider->getVertices(vertices);
#endif
  if (sharedVertexBuffer == nullptr) {
    auto uniqueKey = UniqueKey::Make();
    sharedVertexBuffer =
        std::shared_ptr<GpuBufferProxy>(new GpuBufferProxy(uniqueKey, BufferType::Vertex));
    addResourceProxy(sharedVertexBuffer, uniqueKey);
  }
  return {sharedVertexBuffer, offset};
}

void ProxyProvider::flushSharedVertexBuffer() {
  if (sharedVertexBuffer != nullptr) {
    auto lastBlock = blockBuffer.currentBlock();
    auto data = Data::MakeWithoutCopy(lastBlock.first, lastBlock.second);
    uploadSharedVertexBuffer(std::move(data));
  }
  sharedVertexBufferFlushed = true;
}

void ProxyProvider::clearSharedVertexBuffer() {
  maxValueTracker.addValue(blockBuffer.size());
  blockBuffer.clear(maxValueTracker.getMaxValue());
  sharedVertexBufferFlushed = false;
}

void ProxyProvider::uploadSharedVertexBuffer(std::shared_ptr<Data> data) {
  DEBUG_ASSERT(sharedVertexBuffer != nullptr);
  auto dataSource =
      std::make_unique<AsyncVertexSource>(std::move(data), std::move(sharedVertexBufferTasks));
  auto task = context->drawingBuffer()->make<GpuBufferUploadTask>(
      sharedVertexBuffer->getUniqueKey(), BufferType::Vertex, std::move(dataSource));
  context->drawingManager()->addResourceTask(std::move(task));
  sharedVertexBuffer = nullptr;
}

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
                                                                  AAType aaType,
                                                                  const Rect& clipBounds,
                                                                  uint32_t renderFlags) {
  if (shape == nullptr) {
    return nullptr;
  }
  Matrix drawingMatrix = {};
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
  if (aaType != AAType::None) {
    // Add a 1-pixel outset to preserve anti-aliasing results.
    shapeBounds.outset(1.0f, 1.0f);
  } else {
    static const auto NonAntialiasShapeType = UniqueID::Next();
    uniqueKey = UniqueKey::Append(uniqueKey, &NonAntialiasShapeType, 1);
  }
  auto bounds = isInverseFillType ? clipBounds : shapeBounds;
  drawingMatrix.preTranslate(bounds.x(), bounds.y());
  static const auto TriangleShapeType = UniqueID::Next();
  static const auto TextureShapeType = UniqueID::Next();
  auto triangleKey = UniqueKey::Append(uniqueKey, &TriangleShapeType, 1);
  // The triangle and texture proxies might be created by previous tasks that are still in progress.
  // One of them might not have the corresponding resources in the cache yet, so we need to wrap
  // both of them into the GpuShapeProxy.
  auto triangleProxy = findOrWrapGpuBufferProxy(triangleKey);
  auto textureKey = UniqueKey::Append(uniqueKey, &TextureShapeType, 1);
  auto textureProxy = findOrWrapTextureProxy(textureKey);
  if (triangleProxy != nullptr || textureProxy != nullptr) {
    return std::make_shared<GpuShapeProxy>(drawingMatrix, std::move(triangleProxy),
                                           std::move(textureProxy));
  }
  auto width = static_cast<int>(ceilf(bounds.width()));
  auto height = static_cast<int>(ceilf(bounds.height()));
  shape = Shape::ApplyMatrix(std::move(shape), Matrix::MakeTrans(-bounds.x(), -bounds.y()));
  auto rasterizer = std::make_unique<ShapeRasterizer>(width, height, std::move(shape), aaType);
  std::shared_ptr<DataSource<ShapeBuffer>> dataSource = nullptr;
#ifdef TGFX_USE_THREADS
  if (!(renderFlags & RenderFlags::DisableAsyncTask) && rasterizer->asyncSupport()) {
    dataSource = DataSource<ShapeBuffer>::Async(std::move(rasterizer),
                                                context->drawingBuffer()->getReferenceCounter());
  } else {
    dataSource = std::move(rasterizer);
  }
#else
  dataSource = std::move(rasterizer);
#endif
  auto triangleProxyKey = GetProxyKey(triangleKey, renderFlags);
  auto textureProxyKey = GetProxyKey(textureKey, renderFlags);
  auto task = context->drawingBuffer()->make<ShapeBufferUploadTask>(
      triangleProxyKey, textureProxyKey, std::move(dataSource));
  context->drawingManager()->addResourceTask(std::move(task));
  triangleProxy =
      std::shared_ptr<GpuBufferProxy>(new GpuBufferProxy(triangleProxyKey, BufferType::Vertex));
  addResourceProxy(triangleProxy, triangleKey);
  textureProxy = std::shared_ptr<TextureProxy>(
      new DefaultTextureProxy(textureProxyKey, width, height, false, true));
  addResourceProxy(textureProxy, textureKey);
  return std::make_shared<GpuShapeProxy>(drawingMatrix, triangleProxy, textureProxy);
}

std::shared_ptr<TextureProxy> ProxyProvider::createTextureProxyByImageSource(
    const UniqueKey& uniqueKey, std::shared_ptr<DataSource<ImageBuffer>> source, int width,
    int height, bool alphaOnly, bool mipmapped, uint32_t renderFlags, bool asyncDecoding) {
  auto proxyKey = GetProxyKey(uniqueKey, renderFlags);
  auto task = context->drawingBuffer()->make<TextureUploadTask>(
      proxyKey, std::move(source), mipmapped, asyncDecoding,
      context->drawingBuffer()->getReferenceCounter());
  context->drawingManager()->addResourceTask(std::move(task));
  auto proxy = std::shared_ptr<TextureProxy>(
      new DefaultTextureProxy(proxyKey, width, height, mipmapped, alphaOnly));
  addResourceProxy(proxy, uniqueKey);
  return proxy;
}

std::shared_ptr<TextureProxy> ProxyProvider::createTextureProxy(
    const UniqueKey& uniqueKey, std::shared_ptr<ImageBuffer> imageBuffer, bool mipmapped,
    uint32_t renderFlags) {
  if (imageBuffer == nullptr) {
    return nullptr;
  }
  auto proxy = findOrWrapTextureProxy(uniqueKey);
  if (proxy != nullptr) {
    return proxy;
  }
  auto width = imageBuffer->width();
  auto height = imageBuffer->height();
  auto alphaOnly = imageBuffer->isAlphaOnly();
  auto source = ImageSource::Wrap(std::move(imageBuffer));
  return createTextureProxyByImageSource(uniqueKey, std::move(source), width, height, alphaOnly,
                                         mipmapped, renderFlags);
}

std::shared_ptr<TextureProxy> ProxyProvider::createTextureProxy(
    const UniqueKey& uniqueKey, std::shared_ptr<ImageGenerator> generator, bool mipmapped,
    uint32_t renderFlags) {
  if (generator == nullptr) {
    return nullptr;
  }
  auto proxy = findOrWrapTextureProxy(uniqueKey);
  if (proxy != nullptr) {
    return proxy;
  }
  auto width = generator->width();
  auto height = generator->height();
  auto alphaOnly = generator->isAlphaOnly();
  auto source = ImageSource::MakeFrom(std::move(generator), !mipmapped);
  bool asyncDecoding = false;
#ifdef TGFX_USE_THREADS
  asyncDecoding = !(renderFlags & RenderFlags::DisableAsyncTask);
#endif
  return createTextureProxyByImageSource(uniqueKey, std::move(source), width, height, alphaOnly,
                                         mipmapped, renderFlags, asyncDecoding);
}

std::shared_ptr<TextureProxy> ProxyProvider::createTextureProxy(
    const UniqueKey& uniqueKey, std::shared_ptr<DataSource<ImageBuffer>> source, int width,
    int height, bool alphaOnly, bool mipmapped, uint32_t renderFlags) {
  auto proxy = findOrWrapTextureProxy(uniqueKey);
  if (proxy != nullptr || source == nullptr) {
    return proxy;
  }
  return createTextureProxyByImageSource(uniqueKey, std::move(source), width, height, alphaOnly,
                                         mipmapped, renderFlags);
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
  auto task = context->drawingBuffer()->make<TextureCreateTask>(proxyKey, width, height, format,
                                                                mipmapped, origin);
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
  context->drawingManager()->addTextureFlattenTask(uniqueKey, source);
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
    std::shared_ptr<TextureProxy> textureProxy, PixelFormat format, int sampleCount) {
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto caps = context->caps();
  if (!caps->isFormatRenderable(format)) {
    return nullptr;
  }
  sampleCount = caps->getSampleCount(sampleCount, format);
  auto uniqueKey = UniqueKey::Make();
  auto task = context->drawingBuffer()->make<RenderTargetCreateTask>(uniqueKey, textureProxy,
                                                                     format, sampleCount);
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
