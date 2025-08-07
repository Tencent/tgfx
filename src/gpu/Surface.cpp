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

#include "tgfx/core/Surface.h"
#include "DrawingManager.h"
#include "core/images/TextureImage.h"
#include "core/utils/Log.h"
#include "core/utils/PixelFormatUtil.h"
#include "gpu/ProxyProvider.h"
#include "gpu/RenderContext.h"

namespace tgfx {
std::shared_ptr<Surface> Surface::Make(Context* context, int width, int height, bool alphaOnly,
                                       int sampleCount, bool mipmapped, uint32_t renderFlags) {
  return Make(context, width, height, alphaOnly ? ColorType::ALPHA_8 : ColorType::RGBA_8888,
              sampleCount, mipmapped, renderFlags);
}

std::shared_ptr<Surface> Surface::Make(Context* context, int width, int height, ColorType colorType,
                                       int sampleCount, bool mipmapped, uint32_t renderFlags) {
  if (context == nullptr) {
    return nullptr;
  }
  auto pixelFormat = ColorTypeToPixelFormat(colorType);
  auto proxy = context->proxyProvider()->createRenderTargetProxy({}, width, height, pixelFormat,
                                                                 sampleCount, mipmapped);
  return MakeFrom(std::move(proxy), renderFlags, true);
}

std::shared_ptr<Surface> Surface::MakeFrom(Context* context,
                                           const BackendRenderTarget& renderTarget,
                                           ImageOrigin origin, uint32_t renderFlags) {
  if (context == nullptr) {
    return nullptr;
  }
  auto proxy = RenderTargetProxy::MakeFrom(context, renderTarget, origin);
  return MakeFrom(std::move(proxy), renderFlags);
}

std::shared_ptr<Surface> Surface::MakeFrom(Context* context, const BackendTexture& backendTexture,
                                           ImageOrigin origin, int sampleCount,
                                           uint32_t renderFlags) {
  if (context == nullptr) {
    return nullptr;
  }
  auto proxy =
      context->proxyProvider()->createRenderTargetProxy(backendTexture, sampleCount, origin);
  return MakeFrom(std::move(proxy), renderFlags);
}

std::shared_ptr<Surface> Surface::MakeFrom(Context* context, HardwareBufferRef hardwareBuffer,
                                           int sampleCount, uint32_t renderFlags) {
  if (context == nullptr) {
    return nullptr;
  }
  auto proxy = context->proxyProvider()->createRenderTargetProxy(hardwareBuffer, sampleCount);
  return MakeFrom(std::move(proxy), renderFlags);
}

std::shared_ptr<Surface> Surface::MakeFrom(std::shared_ptr<RenderTargetProxy> renderTargetProxy,
                                           uint32_t renderFlags, bool clearAll) {
  if (renderTargetProxy == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<Surface>(new Surface(std::move(renderTargetProxy), renderFlags, clearAll));
}

Surface::Surface(std::shared_ptr<RenderTargetProxy> proxy, uint32_t renderFlags, bool clearAll)
    : _uniqueID(UniqueID::Next()) {
  DEBUG_ASSERT(proxy != nullptr);
  renderContext = new RenderContext(std::move(proxy), renderFlags, clearAll, this);
}

Surface::~Surface() {
  delete canvas;
  delete renderContext;
}

Context* Surface::getContext() const {
  return renderContext->getContext();
}

uint32_t Surface::renderFlags() const {
  return renderContext->renderFlags;
}

int Surface::width() const {
  return renderContext->renderTarget->width();
}

int Surface::height() const {
  return renderContext->renderTarget->height();
}

ImageOrigin Surface::origin() const {
  return renderContext->renderTarget->origin();
}

BackendRenderTarget Surface::getBackendRenderTarget() {
  getContext()->flush();
  auto renderTarget = renderContext->renderTarget->getRenderTarget();
  if (renderTarget == nullptr) {
    return {};
  }
  return renderTarget->getBackendRenderTarget();
}

BackendTexture Surface::getBackendTexture() {
  auto textureProxy = renderContext->renderTarget->asTextureProxy();
  if (textureProxy == nullptr) {
    return {};
  }
  getContext()->flush();
  auto textureView = textureProxy->getTextureView();
  if (textureView == nullptr) {
    return {};
  }
  return textureView->getBackendTexture();
}

HardwareBufferRef Surface::getHardwareBuffer() {
  auto textureProxy = renderContext->renderTarget->asTextureProxy();
  if (textureProxy == nullptr) {
    return {};
  }
  getContext()->flushAndSubmit(true);
  auto textureView = textureProxy->getTextureView();
  if (textureView == nullptr) {
    return nullptr;
  }
  return textureView->getTexture()->getHardwareBuffer();
}

Canvas* Surface::getCanvas() {
  if (canvas == nullptr) {
    canvas = new Canvas(renderContext, this);
  }
  return canvas;
}

std::shared_ptr<Image> Surface::makeImageSnapshot() {
  if (cachedImage != nullptr) {
    return cachedImage;
  }
  auto renderTarget = renderContext->renderTarget;
  auto drawingManager = getContext()->drawingManager();
  renderContext->flush();
  auto textureProxy = renderTarget->asTextureProxy();
  if (textureProxy == nullptr || renderTarget->externallyOwned()) {
    textureProxy = renderTarget->makeTextureProxy();
    drawingManager->addRenderTargetCopyTask(renderTarget, textureProxy);
  }
  cachedImage = TextureImage::Wrap(std::move(textureProxy));
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
  auto renderTargetProxy = renderContext->renderTarget;
  auto context = renderTargetProxy->getContext();
  context->flush();
  auto textureView = renderTargetProxy->getTextureView();
  auto hardwareBuffer = textureView ? textureView->getTexture()->getHardwareBuffer() : nullptr;
  if (hardwareBuffer != nullptr) {
    context->submit(true);
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
  auto oldContent = discardContent ? nullptr : cachedImage;
  cachedImage = nullptr;
  if (isUnique) {
    return true;
  }
  auto renderTarget = renderContext->renderTarget;
  if (renderTarget->externallyOwned()) {
    return true;
  }
  auto newRenderTarget = renderTarget->makeRenderTargetProxy();
  if (newRenderTarget == nullptr) {
    LOGE("Surface::aboutToDraw(): Failed to make a copy of the renderTarget!");
    return false;
  }
  renderContext->replaceRenderTarget(std::move(newRenderTarget), std::move(oldContent));
  return true;
}
}  // namespace tgfx
