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
#include "core/utils/MathExtra.h"
#include "core/utils/PixelFormatUtil.h"
#include "gpu/DrawingManager.h"
#include "gpu/ProxyProvider.h"
#include "gpu/RenderContext.h"
#include "tgfx/gpu/GPU.h"
#include "tgfx/gpu/Window.h"

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

std::shared_ptr<Surface> Surface::MakeFrom(Context* context, std::shared_ptr<Window> window,
                                           uint32_t renderFlags) {
  if (context == nullptr || window == nullptr) {
    return nullptr;
  }
  // All surfaces of the window share the window's current target (unified frame acquisition):
  // one frame per window per batch, presented once at the end of the batch. A surface created
  // while the batch already uses the frame joins it; otherwise a fresh target is created (for
  // example picking up a window resize).
  auto drawingManager = context->drawingManager();
  auto proxy = drawingManager->createWindowTarget(window);
  if (proxy == nullptr) {
    return nullptr;
  }
  // A frame new to the batch starts with a clear; a frame the batch already uses keeps the
  // drawing recorded on it.
  auto clearAll = !drawingManager->isFrameUsed(proxy);
  auto colorSpace = window->colorSpace();
  return MakeFrom(std::move(proxy), renderFlags, clearAll, std::move(colorSpace),
                  std::move(window));
}

// Whether a surface created for one target of a window can draw into another target of the
// window without changing its visible properties.
static bool CanShareFrame(const RenderTargetProxy& target, const RenderTargetProxy& other) {
  return target.width() == other.width() && target.height() == other.height() &&
         target.format() == other.format() && target.sampleCount() == other.sampleCount() &&
         target.origin() == other.origin();
}

std::shared_ptr<Surface> Surface::MakeFrom(std::shared_ptr<RenderTargetProxy> renderTargetProxy,
                                           uint32_t renderFlags, bool clearAll,
                                           std::shared_ptr<ColorSpace> colorSpace,
                                           std::shared_ptr<Window> window) {
  if (renderTargetProxy == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<Surface>(new Surface(std::move(renderTargetProxy), renderFlags, clearAll,
                                              std::move(colorSpace), std::move(window)));
}

Surface::Surface(std::shared_ptr<RenderTargetProxy> proxy, uint32_t renderFlags, bool clearAll,
                 std::shared_ptr<ColorSpace> colorSpace, std::shared_ptr<Window> window)
    : _uniqueID(UniqueID::Next()), _window(std::move(window)) {
  DEBUG_ASSERT(proxy != nullptr);
  // _window is set before the RenderContext is created, so the initial clear of a new frame is
  // collected through collectFrame() during construction.
  renderContext =
      new RenderContext(std::move(proxy), renderFlags, clearAll, this, std::move(colorSpace));
}

std::shared_ptr<RenderTargetProxy> Surface::resolveFrameTarget(
    DrawingManager* drawingManager, const std::shared_ptr<RenderTargetProxy>& renderTarget) const {
  if (_window == nullptr || drawingManager == nullptr || renderTarget == nullptr) {
    return renderTarget;
  }
  // Surfaces created in earlier batches may hold an older target of the window; they switch to
  // the window's current one, so the batch resolves and presents a single frame.
  auto target = drawingManager->resolveWindowTarget(_window, renderTarget);
  if (target != renderTarget && !CanShareFrame(*target, *renderTarget)) {
    // A surface of a different size was created for the window after this one.
    return nullptr;
  }
  return target;
}

std::shared_ptr<RenderTargetProxy> Surface::collectFrame(
    DrawingManager* drawingManager, const std::shared_ptr<RenderTargetProxy>& renderTarget) {
  if (_window == nullptr || drawingManager == nullptr || renderTarget == nullptr) {
    return renderTarget;
  }
  auto target = resolveFrameTarget(drawingManager, renderTarget);
  if (target == nullptr) {
    return nullptr;
  }
  // Other surfaces of the window may have drawing open on the shared frame; close it first, so
  // the frame's tasks follow the order in which the surfaces recorded their drawing.
  drawingManager->closeCompositors(target);
  // Drawing into the window frame: the batch uses the frame and presents it once at the end of
  // the batch.
  drawingManager->collectFrameUsage(target);
  drawingManager->collectWindow(_window, target);
  return target;
}

std::shared_ptr<RenderTargetProxy> Surface::prepareCopy(DrawingManager* drawingManager) {
  auto renderTarget = renderContext->renderTarget;
  renderContext->flush();
  if (_window == nullptr) {
    return renderTarget;
  }
  auto target = resolveFrameTarget(drawingManager, renderTarget);
  if (target == nullptr) {
    return nullptr;
  }
  if (target != renderTarget) {
    renderContext->replaceRenderTarget(target, nullptr);
  }
  // The copy must capture the drawing every surface of the window recorded on the shared frame
  // before it.
  drawingManager->closeCompositors(target);
  return target;
}

void Surface::collectFrameUsage(DrawingManager* drawingManager,
                                const std::shared_ptr<RenderTargetProxy>& renderTarget) {
  if (_window == nullptr || drawingManager == nullptr || renderTarget == nullptr) {
    return;
  }
  // Reading the window frame (a readback or snapshot copy) only uses it; the presentation stays
  // with the batches that draw into it.
  drawingManager->collectFrameUsage(renderTarget);
}

bool Surface::canReadBack(const std::shared_ptr<RenderTargetProxy>& renderTarget) const {
  if (_window == nullptr) {
    return true;
  }
  if (!_window->supportsReadback() || renderTarget == nullptr) {
    // The window's frame buffer cannot act as a copy source.
    return false;
  }
  // The readback copy must join the batch that draws the frame, so it runs in the submission
  // that presents the frame, after the drawing recorded before it. A flushed batch is fixed and
  // no copy can be added to it anymore.
  auto drawingManager = renderTarget->getContext()->drawingManager();
  return drawingManager->hasPendingPresentation(renderTarget);
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
  auto drawingManager = getContext()->drawingManager();
  auto renderTarget = prepareCopy(drawingManager);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  auto textureProxy = renderTarget->asTextureProxy();
  if (textureProxy == nullptr || renderTarget->externallyOwned()) {
    textureProxy = renderTarget->makeTextureProxy();
    drawingManager->addRenderTargetCopyTask(renderTarget, textureProxy);
    collectFrameUsage(drawingManager, renderTarget);
  }
  cachedImage = TextureImage::Wrap(std::move(textureProxy), colorSpace());
  return cachedImage;
}

RGBA4f<AlphaType::Premultiplied> Surface::getColor(int x, int y,
                                                   std::shared_ptr<ColorSpace> dstColorSpace) {
  auto dstInfo = ImageInfo::Make(1, 1, ColorType::RGBA_F16, AlphaType::Premultiplied, 8,
                                 std::move(dstColorSpace));
  uint16_t color[4];
  if (!readPixels(dstInfo, color, x, y)) {
    return RGBA4f<AlphaType::Premultiplied>::Transparent();
  }
  return {HalfToFloat(color[0]), HalfToFloat(color[1]), HalfToFloat(color[2]),
          HalfToFloat(color[3])};
}

std::shared_ptr<SurfaceReadback> Surface::asyncReadPixels(const Rect& rect) {
  if (rect.isEmpty()) {
    return nullptr;
  }
  auto surfaceRect = Rect::MakeWH(width(), height());
  if (!surfaceRect.contains(rect)) {
    return nullptr;
  }
  auto drawingManager = getContext()->drawingManager();
  auto renderTarget = prepareCopy(drawingManager);
  if (renderTarget == nullptr || !canReadBack(renderTarget)) {
    // The window surface is stale, the window does not support readback, or its frame has not
    // been drawn in the current unflushed batch, so no readback copy can join the batch that
    // draws the frame.
    return nullptr;
  }
  auto srcRect = renderTarget->getOriginTransform().mapRect(rect);
  auto colorType = PixelFormatToColorType(renderTarget->format());
  auto context = renderTarget->getContext();
  auto rowBytes = static_cast<size_t>(srcRect.width()) * ImageInfo::GetBytesPerPixel(colorType);
  auto alignment = static_cast<size_t>(context->gpu()->limits()->minBufferCopyRowAlignment);
  if (alignment > 1) {
    rowBytes = (rowBytes + alignment - 1) & ~(alignment - 1);
  }
  auto info = ImageInfo::Make(static_cast<int>(srcRect.width()), static_cast<int>(srcRect.height()),
                              colorType, AlphaType::Premultiplied, rowBytes, colorSpace());
  auto readbackBuffer = context->proxyProvider()->createReadbackBufferProxy(info.byteSize());
  if (readbackBuffer == nullptr) {
    return nullptr;
  }
  drawingManager->addTransferPixelsTask(renderTarget, srcRect, readbackBuffer);
  collectFrameUsage(drawingManager, renderTarget);
  return std::shared_ptr<SurfaceReadback>(new SurfaceReadback(info, std::move(readbackBuffer)));
}

bool Surface::readPixels(const ImageInfo& dstInfo, void* dstPixels, int srcX, int srcY) {
  auto renderTarget = renderContext->renderTarget;
  auto outInfo = dstInfo.makeIntersect(-srcX, -srcY, renderTarget->width(), renderTarget->height());
  if (outInfo.isEmpty() || dstPixels == nullptr) {
    return false;
  }
  dstPixels = dstInfo.computeOffset(dstPixels, -srcX, -srcY);
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
    // Backends whose buffer mapping is only asynchronous (WebGPU without Asyncify) can never
    // satisfy a synchronous read; use asyncReadPixels() with isReady() polling there.
    LOGE("Surface::readPixels() Failed to read the pixels from the surface!");
    return false;
  }
  auto flipY = renderTarget->origin() == ImageOrigin::BottomLeft;
  // Use the readback's ImageInfo which has the correct aligned rowBytes for GPU buffer layout.
  auto srcInfo = readback->info();
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
