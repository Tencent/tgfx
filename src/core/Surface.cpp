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
#include "core/images/TextureImage.h"
#include "core/utils/CopyPixels.h"
#include "core/utils/Log.h"
#include "core/utils/PixelFormatUtil.h"
#include "gpu/DrawingManager.h"
#include "gpu/ProxyProvider.h"
#include "gpu/RenderContext.h"

namespace tgfx {
std::shared_ptr<Surface> Surface::Make(Context* context, int width, int height, bool alphaOnly,
                                       int sampleCount, bool mipmapped, uint32_t renderFlags,
                                       std::shared_ptr<ColorSpace> colorSpace) {
  return Make(context, width, height, alphaOnly ? ColorType::ALPHA_8 : ColorType::RGBA_8888,
              sampleCount, mipmapped, renderFlags, std::move(colorSpace));
}

std::shared_ptr<Surface> Surface::Make(Context* context, int width, int height, ColorType colorType,
                                       int sampleCount, bool mipmapped, uint32_t renderFlags,
                                       std::shared_ptr<ColorSpace> colorSpace) {
  if (context == nullptr) {
    return nullptr;
  }
  auto pixelFormat = ColorTypeToPixelFormat(colorType);
  auto proxy = context->proxyProvider()->createRenderTargetProxy({}, width, height, pixelFormat,
                                                                 sampleCount, mipmapped);
  return MakeFrom(std::move(proxy), renderFlags, true, std::move(colorSpace));
}

std::shared_ptr<Surface> Surface::MakeFrom(Context* context,
                                           const BackendRenderTarget& renderTarget,
                                           ImageOrigin origin, uint32_t renderFlags,
                                           std::shared_ptr<ColorSpace> colorSpace) {
  if (context == nullptr) {
    return nullptr;
  }
  auto proxy = RenderTargetProxy::MakeFrom(context, renderTarget, origin);
  return MakeFrom(std::move(proxy), renderFlags, false, std::move(colorSpace));
}

std::shared_ptr<Surface> Surface::MakeFrom(Context* context, const BackendTexture& backendTexture,
                                           ImageOrigin origin, int sampleCount,
                                           uint32_t renderFlags,
                                           std::shared_ptr<ColorSpace> colorSpace) {
  if (context == nullptr) {
    return nullptr;
  }
  auto proxy =
      context->proxyProvider()->createRenderTargetProxy(backendTexture, sampleCount, origin);
  return MakeFrom(std::move(proxy), renderFlags, false, std::move(colorSpace));
}

std::shared_ptr<Surface> Surface::MakeFrom(Context* context, HardwareBufferRef hardwareBuffer,
                                           int sampleCount, uint32_t renderFlags,
                                           std::shared_ptr<ColorSpace> colorSpace) {
  if (context == nullptr) {
    return nullptr;
  }
  auto proxy = context->proxyProvider()->createRenderTargetProxy(hardwareBuffer, sampleCount);
  return MakeFrom(std::move(proxy), renderFlags, false, std::move(colorSpace));
}

std::shared_ptr<Surface> Surface::MakeFrom(std::shared_ptr<RenderTargetProxy> renderTargetProxy,
                                           uint32_t renderFlags, bool clearAll,
                                           std::shared_ptr<ColorSpace> colorSpace) {
  if (renderTargetProxy == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<Surface>(
      new Surface(std::move(renderTargetProxy), renderFlags, clearAll, std::move(colorSpace)));
}

Surface::Surface(std::shared_ptr<RenderTargetProxy> proxy, uint32_t renderFlags, bool clearAll,
                 std::shared_ptr<ColorSpace> colorSpace)
    : _uniqueID(UniqueID::Next()) {
  DEBUG_ASSERT(proxy != nullptr);
  renderContext =
      new RenderContext(std::move(proxy), renderFlags, clearAll, this, std::move(colorSpace));
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
  getContext()->flushAndSubmit();
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
  getContext()->flushAndSubmit();
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
  cachedImage = TextureImage::Wrap(std::move(textureProxy), colorSpace());
  return cachedImage;
}

Color Surface::getColor(int x, int y) {
  // 使用 RGBA_F16 格式，不会截断超出 [0,1] 范围的值
  auto dstInfo = ImageInfo::Make(1, 1, ColorType::RGBA_F16, AlphaType::Premultiplied, 8,
                                 ColorSpace::SRGB());
  uint16_t color[4];  // F16 每通道 2 字节
  if (!readPixels(dstInfo, color, x, y)) {
    return Color::Transparent();
  }
  // Convert IEEE 754-2008 half-precision float to single-precision float
  auto halfToFloat = [](uint16_t h) -> float {
    uint32_t sign = static_cast<uint32_t>(h & 0x8000) << 16;
    uint32_t exponent = static_cast<uint32_t>((h >> 10) & 0x1F);
    uint32_t mantissa = static_cast<uint32_t>(h & 0x3FF);
    uint32_t f = 0;
    if (exponent == 0) {
      // Zero or denormalized number, flush to zero
      f = sign;
    } else if (exponent == 31) {
      // Infinity or NaN
      f = sign | 0x7F800000u | (mantissa << 13);
    } else {
      // Normalized number: convert exponent bias from 15 to 127
      f = sign | ((exponent + 112) << 23) | (mantissa << 13);
    }
    float result = 0.0f;
    memcpy(&result, &f, sizeof(float));
    return result;
  };
  return Color(halfToFloat(color[0]), halfToFloat(color[1]),
               halfToFloat(color[2]), halfToFloat(color[3]));
}

std::shared_ptr<SurfaceReadback> Surface::asyncReadPixels(const Rect& rect) {
  if (rect.isEmpty()) {
    return nullptr;
  }
  auto surfaceRect = Rect::MakeWH(width(), height());
  if (!surfaceRect.contains(rect)) {
    return nullptr;
  }
  auto renderTarget = renderContext->renderTarget;
  auto srcRect = renderTarget->getOriginTransform().mapRect(rect);
  auto colorType = PixelFormatToColorType(renderTarget->format());
  auto info = ImageInfo::Make(static_cast<int>(srcRect.width()), static_cast<int>(srcRect.height()),
                              colorType, AlphaType::Premultiplied, 0, colorSpace());
  auto context = renderTarget->getContext();
  auto readbackBuffer = context->proxyProvider()->createReadbackBufferProxy(info.byteSize());
  if (readbackBuffer == nullptr) {
    return nullptr;
  }
  renderContext->flush();
  context->drawingManager()->addTransferPixelsTask(renderTarget, srcRect, readbackBuffer);
  return std::shared_ptr<SurfaceReadback>(new SurfaceReadback(info, std::move(readbackBuffer)));
}

bool Surface::readPixels(const ImageInfo& dstInfo, void* dstPixels, int srcX, int srcY) {
  auto renderTarget = renderContext->renderTarget;
  auto outInfo = dstInfo.makeIntersect(-srcX, -srcY, renderTarget->width(), renderTarget->height());
  if (outInfo.isEmpty() || dstPixels == nullptr) {
    return false;
  }
  dstPixels = dstInfo.computeOffset(dstPixels, -srcX, -srcY);
  auto colorType = PixelFormatToColorType(renderTarget->format());
  auto srcInfo = ImageInfo::Make(outInfo.width(), outInfo.height(), colorType,
                                 AlphaType::Premultiplied, 0, colorSpace());
  auto readX = std::max(0, srcX);
  auto readY = std::max(0, srcY);
  auto rect = Rect::MakeXYWH(readX, readY, outInfo.width(), outInfo.height());
  auto readback = asyncReadPixels(rect);
  if (readback == nullptr) {
    return false;
  }
  auto context = renderTarget->getContext();
  auto srcPixels = readback->lockPixels(context);
  if (srcPixels == nullptr) {
    return false;
  }
  auto flipY = renderTarget->origin() == ImageOrigin::BottomLeft;
  CopyPixels(srcInfo, srcPixels, outInfo, dstPixels, flipY);
  readback->unlockPixels(context);
  return true;
}

const std::shared_ptr<ColorSpace>& Surface::colorSpace() const {
  return renderContext->colorSpace();
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
