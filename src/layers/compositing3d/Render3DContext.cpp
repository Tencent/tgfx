/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "Render3DContext.h"
#include <algorithm>
#include <cmath>
#include "Context3DCompositor.h"
#include "core/Matrix3DUtils.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "layers/BackgroundHandler.h"
#include "layers/BackgroundSource.h"
#include "layers/DrawArgs.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/PictureRecorder.h"
#include "tgfx/core/Surface.h"
#include "tgfx/layers/Layer.h"
#include "tgfx/layers/layerstyles/LayerStyle.h"

namespace tgfx {

namespace {

// BackgroundSource implementation that delegates onGetOwnContents to a Context3DCompositor's
// current target snapshot. Used as the parent source for every fragment's leaf bgSource so
// in-fragment BackgroundBlur dispatches see the BSP-accumulated compositor state plus the
// outer-canvas content that was primed at the start of finishAndDrawTo.
class Compositor3DBackgroundSource : public BackgroundSource {
 public:
  Compositor3DBackgroundSource(Context3DCompositor* compositor, const Matrix& imageMatrix,
                               const Rect& backgroundRect, std::shared_ptr<ColorSpace> colorSpace)
      : BackgroundSource(imageMatrix, backgroundRect, /*surfaceScale=*/1.0f, std::move(colorSpace)),
        _compositor(compositor) {
  }

  Canvas* getCanvas() override {
    return nullptr;
  }

 protected:
  std::shared_ptr<Image> onGetOwnContents() override {
    return _compositor != nullptr ? _compositor->snapshotTarget() : nullptr;
  }

 private:
  Context3DCompositor* _compositor = nullptr;
};

}  // namespace

Render3DContext::Render3DContext(std::shared_ptr<Context3DCompositor> compositor,
                                 const Rect& renderRect, float contentScale,
                                 std::shared_ptr<ColorSpace> colorSpace)
    : Layer3DContext(renderRect, contentScale, std::move(colorSpace)),
      _compositor(std::move(compositor)) {
}

void Render3DContext::emitNode(Layer* layer, const Rect& localBounds, const Matrix3D& transform,
                               float alpha, int depth, bool hasBackgroundStyle) {
  PendingNode node;
  node.layer = layer;
  node.transform = transform;
  node.localBounds = localBounds;
  node.depth = depth;
  node.alpha = alpha;
  node.antialiasing = layer->allowsEdgeAntialiasing();
  node.hasBackgroundStyle = hasBackgroundStyle;
  _pendingNodes.push_back(node);
  _subtreeNeedsBackdrop = _subtreeNeedsBackdrop || hasBackgroundStyle;
}

void Render3DContext::finishAndDrawTo(const DrawArgs& args, Canvas* canvas) {
  DEBUG_ASSERT(!FloatNearlyZero(_contentScale));
  if (_pendingNodes.empty() || args.context == nullptr) {
    return;
  }

  // Rasters are sized to the compositor viewport (not the full localBounds), keeping texture
  // sizes bounded. A fully visible leaf keeps the 1:1 contentScale density; a clipped leaf
  // derives a projected-footprint density so its sample rate matches what the compositor
  // samples.
  const Rect compositorViewport = Rect::MakeWH(static_cast<float>(_compositor->width()),
                                               static_cast<float>(_compositor->height()));
  std::unordered_map<Layer*, RasterInfo> layerRasterInfo;
  layerRasterInfo.reserve(_pendingNodes.size());
  for (const auto& node : _pendingNodes) {
    Matrix3D localToCompositor = node.transform;
    localToCompositor.postScale(_contentScale, _contentScale, 1.0f);
    localToCompositor.postTranslate(-_renderRect.left, -_renderRect.top, 0);
    RasterInfo info;
    if (!ComputeRasterInfo(localToCompositor, node.localBounds, compositorViewport, _contentScale,
                           &info)) {
      continue;
    }
    layerRasterInfo.emplace(node.layer, info);
    _compositor->addPolygon(node.layer, info.visibleLocal, localToCompositor, node.depth,
                            node.alpha, node.antialiasing);
  }

  const auto& fragments = _compositor->prepareTraversal();

  // BackgroundBlur dispatches inside the subtree see two layers of backdrop: outer-canvas content
  // (primed once before the BSP loop into the compositor target) and intra-subtree fragments
  // accumulated as the loop runs. Both contribute via the parent-source path below.
  bool primedFromOuterCanvas = false;
  if (_subtreeNeedsBackdrop) {
    primedFromOuterCanvas = primeCompositorFromOuterCanvas(canvas);
  }

  // Each fragment raster is conceptually a sub-offscreen of an outer Background-aware draw, so we
  // hand its dispatches the same machinery 2D offscreens use: a borrowed sub BackgroundSource
  // backed by the leaf surface, parented to a source that returns the live compositor target
  // snapshot (== outer prime + earlier BSP fragments). dispatch then runs through the standard
  // BackgroundCapturer code path; nested offscreens nested further down derive sub-sub sources
  // through BackgroundCapturer::createSubHandler with no 3D-specific code at all.
  std::shared_ptr<BackgroundSource> compositorSource;
  BackgroundSnapshotMap* outerSnapshots = nullptr;
  auto* outerCapturer = args.backgroundHandler ? args.backgroundHandler->asCapturer() : nullptr;
  if (_subtreeNeedsBackdrop && primedFromOuterCanvas && outerCapturer != nullptr) {
    outerSnapshots = outerCapturer->snapshotMap();
    Matrix compositorImageMatrix = Matrix::MakeScale(1.0f / _contentScale, 1.0f / _contentScale);
    compositorImageMatrix.preTranslate(_renderRect.left, _renderRect.top);
    Rect compositorRect =
        compositorImageMatrix.mapRect(Rect::MakeWH(_renderRect.width(), _renderRect.height()));
    compositorSource = std::make_shared<Compositor3DBackgroundSource>(
        _compositor.get(), compositorImageMatrix, compositorRect, _colorSpace);
  }

  std::unordered_map<Layer*, std::shared_ptr<Image>> layerImages;
  layerImages.reserve(_pendingNodes.size());
  // Pre-compute each pending layer's local→world matrix in this context's world space (== outer
  // canvas-local for the top 3D context; == enclosing leaf's local for a nested 3D context).
  // This is exactly node.transform; the polygon's matrix has been remapped into compositor pixel
  // space and is the wrong basis for the bgSource.
  std::unordered_map<Layer*, Matrix> layerLocalToWorld;
  std::unordered_map<Layer*, bool> layerHasBackgroundStyle;
  layerLocalToWorld.reserve(_pendingNodes.size());
  layerHasBackgroundStyle.reserve(_pendingNodes.size());
  for (const auto& node : _pendingNodes) {
    layerLocalToWorld.emplace(node.layer, node.transform.asMatrix());
    layerHasBackgroundStyle.emplace(node.layer, node.hasBackgroundStyle);
  }
  DrawArgs leafArgs = args;

  for (const auto* fragment : fragments) {
    auto* layer = fragment->layer();
    // Layers carrying BackgroundBlur must raster per-fragment because each BSP slice samples a
    // different mid-traversal compositor state; cached images would drift from what the consume
    // pass expects. Plain layers keep the shared cache for cheap re-use across split fragments.
    auto bgIt = layerHasBackgroundStyle.find(layer);
    bool perFragmentRaster = bgIt != layerHasBackgroundStyle.end() && bgIt->second;
    float rasterAlpha = fragment->rasterAlpha();
    auto worldIt = layerLocalToWorld.find(layer);
    Matrix localToWorld = worldIt != layerLocalToWorld.end() ? worldIt->second : Matrix::I();
    auto infoIt = layerRasterInfo.find(layer);
    if (infoIt == layerRasterInfo.end()) {
      continue;
    }
    const RasterInfo& info = infoIt->second;
    auto blendMode = layer->blendMode();
    std::shared_ptr<Image> image;
    if (perFragmentRaster) {
      image = rasterLayer(layer, rasterAlpha, blendMode, leafArgs, info, compositorSource,
                          outerSnapshots, localToWorld);
    } else {
      auto cacheIt = layerImages.find(layer);
      if (cacheIt == layerImages.end()) {
        image = rasterLayer(layer, rasterAlpha, blendMode, leafArgs, info, compositorSource,
                            outerSnapshots, localToWorld);
        layerImages.emplace(layer, image);
      } else {
        image = cacheIt->second;
      }
    }
    if (image == nullptr) {
      continue;
    }
    _compositor->drawPolygon(fragment, image);
  }

  auto contextImage = _compositor->flushToImage();
  if (contextImage == nullptr) {
    return;
  }

  AutoCanvasRestore autoRestore(canvas);
  auto imageMatrix = Matrix::MakeScale(1.0f / _contentScale, 1.0f / _contentScale);
  imageMatrix.preTranslate(_renderRect.left, _renderRect.top);
  canvas->concat(imageMatrix);
  canvas->drawImage(contextImage);
}

std::shared_ptr<Image> Render3DContext::rasterLayer(
    Layer* layer, float alpha, BlendMode blendMode, DrawArgs& leafArgs, const RasterInfo& info,
    const std::shared_ptr<BackgroundSource>& compositorSource, BackgroundSnapshotMap* snapshots,
    const Matrix& localToWorld) {
  if (info.rasterWidth <= 0 || info.rasterHeight <= 0) {
    return nullptr;
  }
  auto surface = Surface::Make(leafArgs.context, info.rasterWidth, info.rasterHeight, false, 1,
                               true, 0, _colorSpace);
  if (surface == nullptr) {
    return nullptr;
  }
  auto* leafCanvas = surface->getCanvas();
  leafCanvas->setMatrix(info.density);

  // Bind a fresh BackgroundCapturer to a leaf-backed sub source for this fragment when the
  // outer pass is capturing. The sub source treats the leaf as own contents and the compositor
  // target snapshot as its parent — dispatch & nested offscreen handlers then run the standard
  // BackgroundCapturer pipeline with no 3D-specific code paths.
  std::unique_ptr<BackgroundCapturer> leafCapturer;
  std::shared_ptr<BackgroundSource> leafSource;
  if (compositorSource != nullptr && snapshots != nullptr) {
    // leafArgs is reused across fragments; reset to NoOp so a failed createFromSurface call
    // doesn't leave a dangling pointer to the previous iteration's stack-local capturer.
    leafArgs.backgroundHandler = BackgroundHandler::NoOp();
    auto worldBounds = localToWorld.mapRect(info.visibleLocal);
    leafSource =
        compositorSource->createFromSurface(surface.get(), worldBounds, localToWorld, info.density);
    if (leafSource != nullptr) {
      leafCapturer = std::make_unique<BackgroundCapturer>(snapshots, leafSource);
      leafArgs.backgroundHandler = leafCapturer.get();
    }
  }

  // render3DContext signals Layer::drawChildren that we're rasterizing inside a 3D context, so
  // preserve3D middle nodes are not recursed into (they're already registered as polygons).
  // Non-preserve3D leaves still draw their own children normally.
  leafArgs.render3DContext = shared_from_this();
  drawWithFunc(layer, leafArgs, leafCanvas, alpha, blendMode);
  leafArgs.render3DContext = nullptr;
  return surface->makeImageSnapshot();
}

bool Render3DContext::primeCompositorFromOuterCanvas(Canvas* outerCanvas) {
  auto* outerSurface = outerCanvas->getSurface();
  if (outerSurface == nullptr) {
    LOGE("primeCompositorFromOuterCanvas: outer canvas has no surface, 3D backdrop is unavailable");
    return false;
  }
  auto outerSnapshot = outerSurface->makeImageSnapshot();
  if (outerSnapshot == nullptr) {
    return false;
  }
  auto targetWidth = static_cast<int>(_renderRect.width());
  auto targetHeight = static_cast<int>(_renderRect.height());
  if (targetWidth <= 0 || targetHeight <= 0) {
    return false;
  }
  // Map outer-device pixels into compositor pixel space:
  //   compositor pixel → outer canvas-local: Scale(1/contentScale).preTranslate(rr.tl)
  //   outer canvas-local → outer device:   outerCanvas->getMatrix()
  Matrix compositorToOuterDevice = outerCanvas->getMatrix();
  compositorToOuterDevice.preScale(1.0f / _contentScale, 1.0f / _contentScale);
  compositorToOuterDevice.preTranslate(_renderRect.left, _renderRect.top);
  Matrix outerDeviceToCompositor = Matrix::I();
  if (!compositorToOuterDevice.invert(&outerDeviceToCompositor)) {
    return false;
  }
  PictureRecorder recorder = {};
  recorder.beginRecording()->drawImage(std::move(outerSnapshot));
  auto picture = recorder.finishRecordingAsPicture();
  if (picture == nullptr) {
    return false;
  }
  auto primeImage = Image::MakeFrom(std::move(picture), targetWidth, targetHeight,
                                    &outerDeviceToCompositor, _colorSpace);
  if (primeImage != nullptr) {
    _compositor->primeWithImage(primeImage);
  }
  return true;
}

bool Render3DContext::ComputeRasterInfo(const Matrix3D& localToCompositor, const Rect& localBounds,
                                        const Rect& compositorViewport, float contentScale,
                                        RasterInfo* info) {
  if (Matrix3DUtils::IsRectBehindCamera(localBounds, localToCompositor)) {
    return false;
  }
  // Size the raster from the clip-visible local footprint: when it covers the whole leaf, keep the
  // 1:1 contentScale density; otherwise derive density from the (dest / local) ratio of the clipped
  // region, which stays bounded by the viewport. IsRectBehindCamera already rejected near-plane
  // straddles, so the visible region never touches the near plane here and the density stays
  // finite; the homography clip would otherwise blow up at that singular boundary.
  Rect localFootprint = {};
  Rect destFootprint = {};
  if (!Matrix3DUtils::ComputeVisibleFootprints(localBounds, compositorViewport, localToCompositor,
                                               &localFootprint, &destFootprint)) {
    return false;
  }
  // Near-plane clipping can push clipped vertices a hair past localBounds when interpolating in
  // homogeneous space. Clamp again after roundOut so the compositing polygon never expands past
  // the leaf's declared range.
  localFootprint.roundOut();
  if (!localFootprint.intersect(localBounds)) {
    return false;
  }
  destFootprint.roundOut();
  const float localWidth = localFootprint.width();
  const float localHeight = localFootprint.height();
  const float destWidth = destFootprint.width();
  const float destHeight = destFootprint.height();
  if (!(localWidth > 0.0f) || !(localHeight > 0.0f) || !(destWidth > 0.0f) ||
      !(destHeight > 0.0f)) {
    return false;
  }
  const bool coversWholeLeaf = localFootprint == localBounds;
  const float densityX = coversWholeLeaf ? contentScale : destWidth / localWidth;
  const float densityY = coversWholeLeaf ? contentScale : destHeight / localHeight;
  Matrix density = Matrix::MakeScale(densityX, densityY);
  density.postTranslate(-localFootprint.left * densityX, -localFootprint.top * densityY);
  info->visibleLocal = localFootprint;
  info->density = density;
  info->rasterWidth = std::max(1, static_cast<int>(std::ceil(localWidth * densityX)));
  info->rasterHeight = std::max(1, static_cast<int>(std::ceil(localHeight * densityY)));
  return true;
}

}  // namespace tgfx
