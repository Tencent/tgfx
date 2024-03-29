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
#include "gpu/SurfaceDrawContext.h"
#include "images/TextureImage.h"
#include "utils/Log.h"
#include "utils/PixelFormatUtil.h"

namespace tgfx {
std::shared_ptr<Surface> Surface::Make(Context* context, int width, int height, bool alphaOnly,
                                       int sampleCount, bool mipmapped,
                                       const SurfaceOptions* options) {
  return Make(context, width, height, alphaOnly ? ColorType::ALPHA_8 : ColorType::RGBA_8888,
              sampleCount, mipmapped, options);
}

std::shared_ptr<Surface> Surface::Make(Context* context, int width, int height, ColorType colorType,
                                       int sampleCount, bool mipmapped,
                                       const SurfaceOptions* options) {
  auto pixelFormat = ColorTypeToPixelFormat(colorType);
  auto proxy = RenderTargetProxy::Make(context, width, height, pixelFormat, sampleCount, mipmapped);
  auto surface = MakeFrom(std::move(proxy), options);
  if (surface != nullptr) {
    // Clear the surface by default for internally created RenderTarget.
    surface->getCanvas()->clear();
  }
  return surface;
}

std::shared_ptr<Surface> Surface::MakeFrom(Context* context,
                                           const BackendRenderTarget& renderTarget,
                                           ImageOrigin origin, const SurfaceOptions* options) {
  auto proxy = RenderTargetProxy::MakeFrom(context, renderTarget, origin);
  return MakeFrom(std::move(proxy), options);
}

std::shared_ptr<Surface> Surface::MakeFrom(Context* context, const BackendTexture& backendTexture,
                                           ImageOrigin origin, int sampleCount,
                                           const SurfaceOptions* options) {
  auto proxy = RenderTargetProxy::MakeFrom(context, backendTexture, sampleCount, origin);
  return MakeFrom(std::move(proxy), options);
}

std::shared_ptr<Surface> Surface::MakeFrom(Context* context, HardwareBufferRef hardwareBuffer,
                                           int sampleCount, const SurfaceOptions* options) {
  auto proxy = RenderTargetProxy::MakeFrom(context, hardwareBuffer, sampleCount);
  return MakeFrom(std::move(proxy), options);
}

std::shared_ptr<Surface> Surface::MakeFrom(std::shared_ptr<RenderTargetProxy> renderTargetProxy,
                                           const SurfaceOptions* options) {
  if (renderTargetProxy == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<Surface>(new Surface(std::move(renderTargetProxy), options));
}

Surface::Surface(std::shared_ptr<RenderTargetProxy> proxy, const SurfaceOptions* options)
    : renderTargetProxy(std::move(proxy)) {
  DEBUG_ASSERT(this->renderTargetProxy != nullptr);
  if (options != nullptr) {
    surfaceOptions = *options;
  }
  drawContext = new SurfaceDrawContext(this);
  auto rect = Rect::MakeWH(renderTargetProxy->width(), renderTargetProxy->height());
  drawContext->clipRect(rect);
}

Surface::~Surface() {
  delete canvas;
  delete drawContext;
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
    canvas = new Canvas(drawContext);
  }
  return canvas;
}

bool Surface::flush(BackendSemaphore* signalSemaphore) {
  auto context = getContext();
  context->drawingManager()->addTextureResolveTask(renderTargetProxy);
  return context->flush(signalSemaphore);
}

void Surface::flushAndSubmit(bool syncCpu) {
  flush();
  getContext()->submit(syncCpu);
}

std::shared_ptr<Image> Surface::makeImageSnapshot() {
  if (cachedImage != nullptr) {
    return cachedImage;
  }
  auto drawingManager = getContext()->drawingManager();
  drawingManager->addTextureResolveTask(renderTargetProxy);
  auto textureProxy = renderTargetProxy->getTextureProxy();
  if (textureProxy != nullptr && !textureProxy->externallyOwned()) {
    cachedImage = TextureImage::Wrap(std::move(textureProxy));
  } else {
    auto textureCopy = renderTargetProxy->makeTextureProxy();
    drawingManager->addRenderTargetCopyTask(renderTargetProxy, textureCopy,
                                            Rect::MakeWH(width(), height()), Point::Zero());
    cachedImage = TextureImage::Wrap(std::move(textureCopy));
  }
  return cachedImage;
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

bool Surface::aboutToDraw(bool discardContent) {
  if (cachedImage == nullptr) {
    return true;
  }
  auto isUnique = cachedImage.use_count() == 1;
  cachedImage = nullptr;
  if (isUnique) {
    return true;
  }
  auto textureProxy = renderTargetProxy->getTextureProxy();
  if (textureProxy == nullptr || textureProxy->externallyOwned()) {
    return true;
  }
  auto newRenderTargetProxy = renderTargetProxy->makeRenderTargetProxy();
  if (newRenderTargetProxy == nullptr) {
    LOGE("Surface::aboutToDraw(): Failed to make a copy of the renderTarget!");
    return false;
  }
  if (!discardContent) {
    auto newTextureProxy = newRenderTargetProxy->getTextureProxy();
    auto drawingManager = getContext()->drawingManager();
    drawingManager->addRenderTargetCopyTask(renderTargetProxy, newTextureProxy,
                                            Rect::MakeWH(width(), height()), Point::Zero());
  }
  renderTargetProxy = std::move(newRenderTargetProxy);
  drawContext->replaceRenderTarget(renderTargetProxy);
  return true;
}
}  // namespace tgfx
