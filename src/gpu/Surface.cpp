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

#include "tgfx/core/Surface.h"
#include "DrawingManager.h"
#include "core/images/TextureImage.h"
#include "core/utils/Log.h"
#include "core/utils/PixelFormatUtil.h"
#include "core/utils/Profiling.h"
#include "gpu/RenderContext.h"

namespace tgfx {
std::shared_ptr<Surface> Surface::Make(Context* context, int width, int height, bool alphaOnly,
                                       int sampleCount, bool mipmapped, uint32_t renderFlags) {
  TRACE_EVENT_COLOR("Surface::Make", tracy::Color::ColorType::Yellow);
  return Make(context, width, height, alphaOnly ? ColorType::ALPHA_8 : ColorType::RGBA_8888,
              sampleCount, mipmapped, renderFlags);
}

std::shared_ptr<Surface> Surface::Make(Context* context, int width, int height, ColorType colorType,
                                       int sampleCount, bool mipmapped, uint32_t renderFlags) {
  auto pixelFormat = ColorTypeToPixelFormat(colorType);
  auto proxy = RenderTargetProxy::Make(context, width, height, pixelFormat, sampleCount, mipmapped,
                                       ImageOrigin::TopLeft, true);
  return MakeFrom(std::move(proxy), renderFlags);
}

std::shared_ptr<Surface> Surface::MakeFrom(Context* context,
                                           const BackendRenderTarget& renderTarget,
                                           ImageOrigin origin, uint32_t renderFlags) {
  auto proxy = RenderTargetProxy::MakeFrom(context, renderTarget, origin);
  return MakeFrom(std::move(proxy), renderFlags);
}

std::shared_ptr<Surface> Surface::MakeFrom(Context* context, const BackendTexture& backendTexture,
                                           ImageOrigin origin, int sampleCount,
                                           uint32_t renderFlags) {
  auto proxy = RenderTargetProxy::MakeFrom(context, backendTexture, sampleCount, origin);
  return MakeFrom(std::move(proxy), renderFlags);
}

std::shared_ptr<Surface> Surface::MakeFrom(Context* context, HardwareBufferRef hardwareBuffer,
                                           int sampleCount, uint32_t renderFlags) {
  auto proxy = RenderTargetProxy::MakeFrom(context, hardwareBuffer, sampleCount);
  return MakeFrom(std::move(proxy), renderFlags);
}

std::shared_ptr<Surface> Surface::MakeFrom(std::shared_ptr<RenderTargetProxy> renderTargetProxy,
                                           uint32_t renderFlags) {
  if (renderTargetProxy == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<Surface>(new Surface(std::move(renderTargetProxy), renderFlags));
}

Surface::Surface(std::shared_ptr<RenderTargetProxy> proxy, uint32_t renderFlags)
    : _uniqueID(UniqueID::Next()), renderTargetProxy(std::move(proxy)), _renderFlags(renderFlags) {
  DEBUG_ASSERT(this->renderTargetProxy != nullptr);
}

Surface::~Surface() {
  delete canvas;
  delete renderContext;
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
  auto context = getContext();
  context->drawingManager()->addTextureResolveTask(renderTargetProxy);
  context->flush();
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
  auto context = getContext();
  context->drawingManager()->addTextureResolveTask(renderTargetProxy);
  context->flush();
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
  auto context = getContext();
  context->drawingManager()->addTextureResolveTask(renderTargetProxy);
  context->flushAndSubmit(true);
  auto texture = renderTargetProxy->getTexture();
  if (texture == nullptr) {
    return nullptr;
  }
  return texture->getHardwareBuffer();
}

static Path GetInitClip(int width, int height) {
  Path path = {};
  path.addRect(Rect::MakeWH(width, height));
  return path;
}

Canvas* Surface::getCanvas() {
  if (canvas == nullptr) {
    renderContext = new RenderContext(this);
    canvas = new Canvas(renderContext, GetInitClip(width(), height()));
  }
  return canvas;
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
  auto context = getContext();
  context->drawingManager()->addTextureResolveTask(renderTargetProxy);
  context->flush();
  auto texture = renderTargetProxy->getTexture();
  auto hardwareBuffer = texture ? texture->getHardwareBuffer() : nullptr;
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

uint32_t Surface::contentVersion() const {
  if (renderContext == nullptr) {
    return 1u;
  }
  return renderContext->opContext->contentVersion();
}

bool Surface::aboutToDraw(const std::function<bool()>& willDiscardContent) {
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
  if (!willDiscardContent()) {
    auto newTextureProxy = newRenderTargetProxy->getTextureProxy();
    auto drawingManager = getContext()->drawingManager();
    drawingManager->addRenderTargetCopyTask(renderTargetProxy, newTextureProxy,
                                            Rect::MakeWH(width(), height()), Point::Zero());
  }
  renderTargetProxy = std::move(newRenderTargetProxy);
  renderContext->replaceRenderTarget(renderTargetProxy);
  return true;
}
}  // namespace tgfx
