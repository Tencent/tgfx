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

#include "ProxyProvider.h"
#include "core/ShapeRasterizer.h"
#include "core/shapes/MatrixShape.h"
#include "core/utils/MathExtra.h"
#include "core/utils/USE.h"
#include "core/utils/UniqueID.h"
#include "gpu/DrawingManager.h"
#include "gpu/proxies/BackendTextureRenderTargetProxy.h"
#include "gpu/proxies/DefaultTextureProxy.h"
#include "gpu/proxies/HardwareRenderTargetProxy.h"
#include "gpu/proxies/TextureRenderTargetProxy.h"
#include "gpu/tasks/GPUBufferUploadTask.h"
#include "gpu/tasks/ShapeBufferUploadTask.h"
#include "gpu/tasks/TextureUploadTask.h"
#include "tgfx/core/RenderFlags.h"

namespace tgfx {
ProxyProvider::ProxyProvider(Context* context)
    : context(context), vertexBlockBuffer(1 << 14, 1 << 21) {  // 16kb, 2MB
}

std::shared_ptr<GPUBufferProxy> ProxyProvider::createGPUBufferProxy(const UniqueKey& uniqueKey,
                                                                    std::shared_ptr<Data> data,
                                                                    BufferType bufferType,
                                                                    uint32_t renderFlags) {
  if (data == nullptr || data->empty()) {
    return nullptr;
  }
  auto source = DataSource<Data>::Wrap(std::move(data));
  renderFlags |= RenderFlags::DisableAsyncTask;
  return createGPUBufferProxy(uniqueKey, std::move(source), bufferType, renderFlags);
}

std::shared_ptr<GPUBufferProxy> ProxyProvider::createGPUBufferProxy(
    const UniqueKey& uniqueKey, std::unique_ptr<DataSource<Data>> source, BufferType bufferType,
    uint32_t renderFlags) {
  if (source == nullptr) {
    return nullptr;
  }
  auto proxy = findOrWrapGPUBufferProxy(uniqueKey);
  if (proxy != nullptr) {
    return proxy;
  }
#ifdef TGFX_USE_THREADS
  if (!(renderFlags & RenderFlags::DisableAsyncTask)) {
    source = DataSource<Data>::Async(std::move(source));
  }
#endif
  proxy = std::shared_ptr<GPUBufferProxy>(new GPUBufferProxy(bufferType));
  addResourceProxy(proxy, uniqueKey);
  auto task =
      context->drawingBuffer()->make<GPUBufferUploadTask>(proxy, bufferType, std::move(source));
  context->drawingManager()->addResourceTask(std::move(task), uniqueKey, renderFlags);
  return proxy;
}

std::shared_ptr<VertexBufferProxy> ProxyProvider::createVertexBuffer(
    PlacementPtr<VertexProvider> provider, uint32_t renderFlags) {
  if (provider == nullptr) {
    return nullptr;
  }
  DEBUG_ASSERT(!sharedVertexBufferFlushed);
  auto byteSize = provider->vertexCount() * sizeof(float);
  auto lastBlock = vertexBlockBuffer.currentBlock();
  auto vertices = reinterpret_cast<float*>(vertexBlockBuffer.allocate(byteSize));
  if (vertices == nullptr) {
    LOGE("ProxyProvider::createVertexBuffer() Failed to allocate memory!");
    return nullptr;
  }
  auto offset = lastBlock.second;
  auto currentBlock = vertexBlockBuffer.currentBlock();
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
    sharedVertexBuffer = std::shared_ptr<GPUBufferProxy>(new GPUBufferProxy(BufferType::Vertex));
    addResourceProxy(sharedVertexBuffer);
  }
  return std::make_shared<VertexBufferProxy>(sharedVertexBuffer, offset, byteSize);
}

void ProxyProvider::flushSharedVertexBuffer() {
  if (sharedVertexBuffer != nullptr) {
    auto lastBlock = vertexBlockBuffer.currentBlock();
    auto data = Data::MakeWithoutCopy(lastBlock.first, lastBlock.second);
    uploadSharedVertexBuffer(std::move(data));
  }
  sharedVertexBufferFlushed = true;
}

void ProxyProvider::clearSharedVertexBuffer() {
  maxValueTracker.addValue(vertexBlockBuffer.size());
  vertexBlockBuffer.clear(maxValueTracker.getMaxValue());
  sharedVertexBufferFlushed = false;
}

void ProxyProvider::uploadSharedVertexBuffer(std::shared_ptr<Data> data) {
  DEBUG_ASSERT(sharedVertexBuffer != nullptr);
  auto dataSource =
      std::make_unique<AsyncVertexSource>(std::move(data), std::move(sharedVertexBufferTasks));
  auto task = context->drawingBuffer()->make<GPUBufferUploadTask>(
      sharedVertexBuffer, BufferType::Vertex, std::move(dataSource));
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

std::shared_ptr<GPUShapeProxy> ProxyProvider::createGPUShapeProxy(std::shared_ptr<Shape> shape,
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
    // Add a 1-pixel outset to preserve antialiasing results.
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
  // both of them into the GPUShapeProxy.
  auto triangleProxy = findOrWrapGPUBufferProxy(triangleKey);
  auto textureKey = UniqueKey::Append(uniqueKey, &TextureShapeType, 1);
  auto textureProxy = findOrWrapTextureProxy(textureKey);
  if (triangleProxy != nullptr || textureProxy != nullptr) {
    return std::make_shared<GPUShapeProxy>(drawingMatrix, std::move(triangleProxy),
                                           std::move(textureProxy));
  }
  auto width = static_cast<int>(ceilf(bounds.width()));
  auto height = static_cast<int>(ceilf(bounds.height()));
  shape = Shape::ApplyMatrix(std::move(shape), Matrix::MakeTrans(-bounds.x(), -bounds.y()));
  auto rasterizer = std::make_unique<ShapeRasterizer>(width, height, std::move(shape), aaType);
  std::unique_ptr<DataSource<ShapeBuffer>> dataSource = nullptr;
#ifdef TGFX_USE_THREADS
  if (!(renderFlags & RenderFlags::DisableAsyncTask) && rasterizer->asyncSupport()) {
    dataSource = DataSource<ShapeBuffer>::Async(std::move(rasterizer));
  } else {
    dataSource = std::move(rasterizer);
  }
#else
  dataSource = std::move(rasterizer);
#endif
  triangleProxy = std::shared_ptr<GPUBufferProxy>(new GPUBufferProxy(BufferType::Vertex));
  addResourceProxy(triangleProxy, triangleKey);
  textureProxy =
      std::shared_ptr<TextureProxy>(new TextureProxy(width, height, PixelFormat::ALPHA_8, true));
  addResourceProxy(textureProxy, textureKey);
  auto task = context->drawingBuffer()->make<ShapeBufferUploadTask>(triangleProxy, textureProxy,
                                                                    std::move(dataSource));
  if (!(renderFlags & RenderFlags::DisableCache)) {
    task->textureProxy = textureProxy;
  }
  context->drawingManager()->addResourceTask(std::move(task), triangleKey, renderFlags);
  return std::make_shared<GPUShapeProxy>(drawingMatrix, triangleProxy, textureProxy);
}

std::shared_ptr<TextureProxy> ProxyProvider::createTextureProxyByImageSource(
    const UniqueKey& uniqueKey, std::shared_ptr<DataSource<ImageBuffer>> source, int width,
    int height, bool alphaOnly, bool mipmapped, uint32_t renderFlags) {
  auto format = alphaOnly ? PixelFormat::ALPHA_8 : PixelFormat::Unknown;
  auto proxy = std::shared_ptr<TextureProxy>(new TextureProxy(width, height, format, mipmapped));
  addResourceProxy(proxy, uniqueKey);
  auto task =
      context->drawingBuffer()->make<TextureUploadTask>(proxy, std::move(source), mipmapped);
  context->drawingManager()->addResourceTask(std::move(task), uniqueKey, renderFlags);
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
#ifdef TGFX_USE_THREADS
  auto asyncDecoding = !(renderFlags & RenderFlags::DisableAsyncTask);
#else
  auto asyncDecoding = false;
#endif
  // Ensure the image source is retained so it won't be destroyed prematurely during async decoding.
  auto source = ImageSource::MakeFrom(std::move(generator), !mipmapped, asyncDecoding);
  return createTextureProxyByImageSource(uniqueKey, std::move(source), width, height, alphaOnly,
                                         mipmapped, renderFlags);
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

static constexpr int MinApproxSize = 16;
static constexpr int MagicTol = 1024;

/**
 * Map 'value' to a larger multiple of 2. Values <= 'MagicTol' will pop up to the next power of 2.
 * Those above 'MagicTol' will only go up half the floor power of 2.
 */
int GetApproxSize(int value) {
  value = std::max(MinApproxSize, value);
  if (IsPow2(value)) {
    return value;
  }
  int ceilPow2 = NextPow2(value);
  if (value <= MagicTol) {
    return ceilPow2;
  }
  int floorPow2 = ceilPow2 >> 1;
  int mid = floorPow2 + (floorPow2 >> 1);
  if (value <= mid) {
    return mid;
  }
  return ceilPow2;
}

std::shared_ptr<TextureProxy> ProxyProvider::createTextureProxy(
    const UniqueKey& uniqueKey, int width, int height, PixelFormat format, bool mipmapped,
    ImageOrigin origin, BackingFit backingFit, uint32_t renderFlags) {
  if (!Texture::CheckSizeAndFormat(context, width, height, format)) {
    return nullptr;
  }
  if (auto proxy = findOrWrapTextureProxy(uniqueKey)) {
    proxy->_width = width;
    proxy->_height = height;
    return proxy;
  }
  auto hasMipmaps = context->caps()->mipmapSupport ? mipmapped : false;
  auto textureProxy = std::shared_ptr<DefaultTextureProxy>(
      new DefaultTextureProxy(width, height, format, hasMipmaps, origin));
  if (backingFit == BackingFit::Approx) {
    textureProxy->_backingStoreWidth = GetApproxSize(width);
    textureProxy->_backingStoreHeight = GetApproxSize(height);
  }
  if (!(renderFlags & RenderFlags::DisableCache)) {
    textureProxy->uniqueKey = uniqueKey;
  }
  addResourceProxy(textureProxy, uniqueKey);
  return textureProxy;
}

std::shared_ptr<TextureProxy> ProxyProvider::wrapBackendTexture(
    const BackendTexture& backendTexture, ImageOrigin origin, bool adopted) {
  auto texture = Texture::MakeFrom(context, backendTexture, origin, adopted);
  if (texture == nullptr) {
    return nullptr;
  }
  auto format = TextureSampler::GetPixelFormat(backendTexture);
  auto proxy = std::shared_ptr<TextureProxy>(new TextureProxy(
      texture->width(), texture->height(), format, texture->hasMipmaps(), texture->origin()));
  proxy->resource = std::move(texture);
  addResourceProxy(proxy);
  return proxy;
}

std::shared_ptr<RenderTargetProxy> ProxyProvider::createRenderTargetProxy(
    const BackendTexture& backendTexture, int sampleCount, ImageOrigin origin, bool adopted) {
  auto format = TextureSampler::GetPixelFormat(backendTexture);
  if (format == PixelFormat::Unknown) {
    return nullptr;
  }
  auto caps = context->caps();
  if (!caps->isFormatRenderable(format)) {
    return nullptr;
  }
  sampleCount = caps->getSampleCount(sampleCount, format);
  auto proxy = std::shared_ptr<TextureRenderTargetProxy>(
      new BackendTextureRenderTargetProxy(backendTexture, format, sampleCount, origin, adopted));
  addResourceProxy(proxy);
  return proxy;
}

std::shared_ptr<RenderTargetProxy> ProxyProvider::createRenderTargetProxy(
    HardwareBufferRef hardwareBuffer, int sampleCount) {
  auto size = HardwareBufferGetSize(hardwareBuffer);
  if (size.isEmpty()) {
    return nullptr;
  }
  auto format = TextureSampler::GetPixelFormat(hardwareBuffer);
  if (format == PixelFormat::Unknown) {
    return nullptr;
  }
  auto caps = context->caps();
  if (!caps->isFormatRenderable(format)) {
    return nullptr;
  }
  sampleCount = caps->getSampleCount(sampleCount, format);
  auto proxy = std::shared_ptr<TextureRenderTargetProxy>(
      new HardwareRenderTargetProxy(hardwareBuffer, size.width, size.height, format, sampleCount));
  addResourceProxy(proxy);
  return proxy;
}

std::shared_ptr<RenderTargetProxy> ProxyProvider::createRenderTargetProxy(
    const UniqueKey& uniqueKey, int width, int height, PixelFormat format, int sampleCount,
    bool mipmapped, ImageOrigin origin, BackingFit backingFit, uint32_t renderFlags) {
  if (!Texture::CheckSizeAndFormat(context, width, height, format)) {
    return nullptr;
  }
  if (auto proxy = findOrWrapTextureProxy(uniqueKey)) {
    proxy->_width = width;
    proxy->_height = height;
    return proxy->asRenderTargetProxy();
  }
  auto caps = context->caps();
  if (!caps->isFormatRenderable(format)) {
    return nullptr;
  }
  sampleCount = caps->getSampleCount(sampleCount, format);
  auto hasMipmaps = caps->mipmapSupport ? mipmapped : false;
  auto proxy = std::shared_ptr<TextureRenderTargetProxy>(
      new TextureRenderTargetProxy(width, height, format, sampleCount, hasMipmaps, origin));
  if (backingFit == BackingFit::Approx) {
    proxy->_backingStoreWidth = GetApproxSize(width);
    proxy->_backingStoreHeight = GetApproxSize(height);
  }
  if (!(renderFlags & RenderFlags::DisableCache)) {
    proxy->uniqueKey = uniqueKey;
  }
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

std::shared_ptr<GPUBufferProxy> ProxyProvider::findOrWrapGPUBufferProxy(
    const UniqueKey& uniqueKey) {
  auto proxy = std::static_pointer_cast<GPUBufferProxy>(findProxy(uniqueKey));
  if (proxy != nullptr) {
    return proxy;
  }
  auto gpuBuffer = Resource::Find<GPUBuffer>(context, uniqueKey);
  if (gpuBuffer == nullptr) {
    return nullptr;
  }
  proxy = std::shared_ptr<GPUBufferProxy>(new GPUBufferProxy(gpuBuffer->bufferType()));
  proxy->resource = std::move(gpuBuffer);
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
  if (auto renderTarget = texture->asRenderTarget()) {
    proxy = std::shared_ptr<TextureProxy>(new TextureRenderTargetProxy(
        texture->width(), texture->height(), renderTarget->format(), renderTarget->sampleCount(),
        texture->hasMipmaps(), texture->origin(), renderTarget->externallyOwned()));
  } else {
    auto format = texture->isYUV() ? PixelFormat::Unknown : texture->getSampler()->format();
    proxy = std::shared_ptr<TextureProxy>(new TextureProxy(
        texture->width(), texture->height(), format, texture->hasMipmaps(), texture->origin()));
  }
  proxy->resource = std::move(texture);
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
  if (!uniqueKey.empty()) {
    proxyMap[uniqueKey] = proxy;
  }
}

}  // namespace tgfx
