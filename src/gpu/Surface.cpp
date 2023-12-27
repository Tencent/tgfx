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

#include "tgfx/gpu/Surface.h"
#include "DrawingManager.h"
#include "core/PixelBuffer.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "utils/Log.h"
#include "utils/PixelFormatUtil.h"

namespace tgfx {
std::shared_ptr<Surface> Surface::Make(Context* context, int width, int height, bool alphaOnly,
                                       int sampleCount, bool mipMapped,
                                       const SurfaceOptions* options) {
  return Make(context, width, height, alphaOnly ? ColorType::ALPHA_8 : ColorType::RGBA_8888,
              sampleCount, mipMapped, options);
}

std::shared_ptr<Surface> Surface::Make(Context* context, int width, int height, ColorType colorType,
                                       int sampleCount, bool mipMapped,
                                       const SurfaceOptions* options) {
  auto pixelFormat = ColorTypeToPixelFormat(colorType);
  auto proxy = RenderTargetProxy::Make(context, width, height, pixelFormat, sampleCount, mipMapped);
  if (proxy == nullptr) {
    return nullptr;
  }
  auto surface = new Surface(std::move(proxy), options);
  // Clear the surface by default for internally created RenderTarget.
  surface->getCanvas()->clear();
  return std::shared_ptr<Surface>(surface);
}

std::shared_ptr<Surface> Surface::MakeFrom(Context* context,
                                           const BackendRenderTarget& renderTarget,
                                           ImageOrigin origin, const SurfaceOptions* options) {
  auto rt = RenderTarget::MakeFrom(context, renderTarget, origin);
  return MakeFrom(std::move(rt), options);
}

std::shared_ptr<Surface> Surface::MakeFrom(Context* context, const BackendTexture& backendTexture,
                                           ImageOrigin origin, int sampleCount,
                                           const SurfaceOptions* options) {
  auto texture = Texture::MakeFrom(context, backendTexture, origin);
  return MakeFrom(std::move(texture), sampleCount, options);
}

std::shared_ptr<Surface> Surface::MakeFrom(Context* context, HardwareBufferRef hardwareBuffer,
                                           int sampleCount, const SurfaceOptions* options) {
  auto texture = Texture::MakeFrom(context, hardwareBuffer);
  return MakeFrom(std::move(texture), sampleCount, options);
}

std::shared_ptr<Surface> Surface::MakeFrom(std::shared_ptr<RenderTarget> renderTarget,
                                           const SurfaceOptions* options) {
  auto proxy = RenderTargetProxy::MakeFrom(std::move(renderTarget));
  if (proxy == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<Surface>(new Surface(std::move(proxy), options));
}

std::shared_ptr<Surface> Surface::MakeFrom(std::shared_ptr<Texture> texture, int sampleCount,
                                           const SurfaceOptions* options) {
  auto proxy = RenderTargetProxy::MakeFrom(std::move(texture), sampleCount);
  if (proxy == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<Surface>(new Surface(std::move(proxy), options));
}

Surface::Surface(std::shared_ptr<RenderTargetProxy> proxy, const SurfaceOptions* options)
    : renderTargetProxy(std::move(proxy)) {
  DEBUG_ASSERT(this->renderTargetProxy != nullptr);
  if (options != nullptr) {
    surfaceOptions = *options;
  }
}

Surface::~Surface() {
  delete canvas;
}

Context* Surface::getContext() const {
  return renderTargetProxy->getContext();
}

int Surface::width() const {
  return renderTargetProxy->width();
}

int Surface::height() const {
  return renderTargetProxy->height();
}

ImageOrigin Surface::origin() const {
  return renderTargetProxy->origin();
}

std::shared_ptr<Texture> Surface::getTexture() {
  if (!renderTargetProxy->isTextureBacked()) {
    return nullptr;
  }
  flush();
  return renderTargetProxy->getTexture();
}

BackendRenderTarget Surface::getBackendRenderTarget() {
  flush();
  auto renderTarget = renderTargetProxy->getRenderTarget();
  if (renderTarget == nullptr) {
    return {};
  }
  return renderTarget->getBackendRenderTarget();
}

BackendTexture Surface::getBackendTexture() {
  if (!renderTargetProxy->isTextureBacked()) {
    return {};
  }
  flush();
  auto texture = renderTargetProxy->getTexture();
  if (texture == nullptr) {
    return {};
  }
  return texture->getBackendTexture();
}

HardwareBufferRef Surface::getHardwareBuffer() {
  if (!renderTargetProxy->isTextureBacked()) {
    return nullptr;
  }
  flushAndSubmit(true);
  auto texture = renderTargetProxy->getTexture();
  if (texture == nullptr) {
    return nullptr;
  }
  return texture->getHardwareBuffer();
}

bool Surface::wait(const BackendSemaphore& waitSemaphore) {
  return renderTargetProxy->getContext()->wait(waitSemaphore);
}

Canvas* Surface::getCanvas() {
  if (canvas == nullptr) {
    canvas = new Canvas(this);
  }
  return canvas;
}

bool Surface::flush(BackendSemaphore* signalSemaphore) {
  auto semaphore = Semaphore::Wrap(signalSemaphore);
  auto drawingManager = renderTargetProxy->getContext()->drawingManager();
  drawingManager->newTextureResolveRenderTask(renderTargetProxy);
  auto result = drawingManager->flush(semaphore.get());
  if (signalSemaphore != nullptr) {
    *signalSemaphore = semaphore->getBackendSemaphore();
  }
  return result;
}

void Surface::flushAndSubmit(bool syncCpu) {
  flush();
  renderTargetProxy->getContext()->submit(syncCpu);
}

std::shared_ptr<Image> Surface::makeImageSnapshot() {
  flush();
  if (cachedImage != nullptr) {
    return cachedImage;
  }
  if (renderTargetProxy->externallyOwned()) {
    auto textureCopy = renderTargetProxy->makeTextureProxy(true);
    cachedImage = Image::MakeFrom(textureCopy->getTexture());
  } else {
    auto texture = renderTargetProxy->getTexture();
    cachedImage = Image::MakeFrom(texture);
  }
  return cachedImage;
}

void Surface::aboutToDraw(bool discardContent) {
  if (cachedImage == nullptr) {
    return;
  }
  auto isUnique = cachedImage.use_count() == 1;
  cachedImage = nullptr;
  if (isUnique) {
    return;
  }
  if (renderTargetProxy->externallyOwned()) {
    return;
  }
  getContext()->drawingManager()->closeActiveOpsTask();
  auto newRenderTargetProxy = renderTargetProxy->makeRenderTargetProxy(!discardContent);
  if (newRenderTargetProxy == nullptr) {
    LOGE("Surface::aboutToDraw(): Failed to make a copy of the renderTarget!");
    return;
  }
  renderTargetProxy = newRenderTargetProxy;
}

Color Surface::getColor(int x, int y) {
  uint8_t color[4];
  auto info = ImageInfo::Make(1, 1, ColorType::RGBA_8888, AlphaType::Premultiplied);
  if (!readPixels(info, color, x, y)) {
    return Color::Transparent();
  }
  return Color::FromRGBA(color[0], color[1], color[2], color[3]);
}

bool Surface::readPixels(const ImageInfo& dstInfo, void* dstPixels, int srcX, int srcY) {
  if (dstInfo.isEmpty() || dstPixels == nullptr) {
    return false;
  }
  flush();
  auto texture = renderTargetProxy->getTexture();
  auto hardwareBuffer = texture ? texture->getHardwareBuffer() : nullptr;
  if (hardwareBuffer != nullptr) {
    getContext()->submit(true);
    auto pixels = HardwareBufferLock(hardwareBuffer);
    if (pixels != nullptr) {
      auto info = HardwareBufferGetInfo(hardwareBuffer);
      auto success = Pixmap(info, pixels).readPixels(dstInfo, dstPixels, srcX, srcY);
      HardwareBufferUnlock(hardwareBuffer);
      return success;
    }
  }
  auto renderTarget = renderTargetProxy->getRenderTarget();
  if (renderTarget == nullptr) {
    return false;
  }
  return renderTarget->readPixels(dstInfo, dstPixels, srcX, srcY);
}
}  // namespace tgfx
