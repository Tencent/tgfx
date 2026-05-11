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

#include "BackgroundSource.h"
#include <utility>
#include "core/filters/GaussianBlurImageFilter.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "tgfx/core/PictureRecorder.h"

namespace tgfx {

namespace {

// Geometry shared by top-level Make of both Surface and Picture variants.
struct TopLevelGeometry {
  Matrix surfaceMatrix = Matrix::I();  // local → device pixel (applied to the canvas)
  Matrix imageMatrix = Matrix::I();    // image pixel → world
  Rect imageRect = Rect::MakeEmpty();  // world-space rect the image covers
  int width = 0;
  int height = 0;
  float surfaceScale = 1.0f;
  bool valid = false;
};

static float ComputeMaxBlurOutset() {
  auto filter =
      ImageFilter::Blur(GaussianBlurImageFilter::MaxSigma(), GaussianBlurImageFilter::MaxSigma());
  return filter->filterBounds(Rect::MakeEmpty()).right;
}

static float MaxBlurOutset() {
  static const float MaxOutset = ComputeMaxBlurOutset();
  return MaxOutset;
}

static TopLevelGeometry ComputeTopLevelGeometry(const Rect& drawRect, float maxOutset,
                                                float minOutset, const Matrix& matrix) {
  TopLevelGeometry out;
  auto rect = drawRect;
  rect.outset(maxOutset, maxOutset);
  rect.roundOut();
  // Down-sample uniformly when blur outset would push the surface past the single-pass budget.
  out.surfaceScale = 1.0f;
  auto maxBlurOutset = MaxBlurOutset();
  if (minOutset > maxBlurOutset) {
    out.surfaceScale = maxBlurOutset / minOutset;
  }
  rect.scale(out.surfaceScale, out.surfaceScale);
  rect.roundOut();
  out.surfaceMatrix = Matrix::MakeTrans(-rect.x(), -rect.y());
  out.surfaceMatrix.preScale(out.surfaceScale, out.surfaceScale);
  out.surfaceMatrix.preConcat(matrix);
  if (!out.surfaceMatrix.invert(&out.imageMatrix)) {
    return out;
  }
  out.imageRect = Rect::MakeWH(rect.width(), rect.height());
  out.imageMatrix.mapRect(&out.imageRect);
  out.width = static_cast<int>(rect.width());
  out.height = static_cast<int>(rect.height());
  out.valid = out.width > 0 && out.height > 0;
  return out;
}

// Geometry shared by createFromSurface() / createFromPicture() of both variants.
struct SubGeometry {
  Matrix childImageMatrix = Matrix::I();     // sub image pixel → world
  Rect childWorldRect = Rect::MakeEmpty();   // sub's bounds in world coords
  Point childSurfaceOffset = Point::Zero();  // sub surface origin in parent image-pixel coords
  // Sub surface origin in carrier-surface-pixel coords (the borrowed PictureRecorder's drawing
  // space). Picture-backed sub uses this to translate its image origin to the layer's slice.
  Point childSurfaceTopLeft = Point::Zero();
  int width = 0;
  int height = 0;
  bool valid = false;
};

static SubGeometry ComputeSubGeometry(const Matrix& parentImageMatrix, const Rect& renderBounds,
                                      const Matrix& localToWorld, const Matrix& localToSurface) {
  SubGeometry out;
  if (renderBounds.isEmpty()) {
    return out;
  }
  auto childWorldRect = renderBounds;
  childWorldRect.roundOut();

  Matrix worldToLocal = Matrix::I();
  if (!localToWorld.invert(&worldToLocal)) {
    return out;
  }
  auto childLocalRect = worldToLocal.mapRect(childWorldRect);
  childLocalRect.roundOut();
  auto childSurfaceRect = localToSurface.mapRect(childLocalRect);
  childSurfaceRect.roundOut();
  if (childSurfaceRect.isEmpty()) {
    return out;
  }

  out.childSurfaceTopLeft = Point::Make(childSurfaceRect.x(), childSurfaceRect.y());

  Matrix worldToParentImage = Matrix::I();
  if (!parentImageMatrix.invert(&worldToParentImage)) {
    return out;
  }
  auto childRectInParentImage = worldToParentImage.mapRect(childWorldRect);
  childRectInParentImage.roundOut();
  out.childSurfaceOffset = Point::Make(childRectInParentImage.x(), childRectInParentImage.y());

  out.childImageMatrix = parentImageMatrix;
  out.childImageMatrix.preTranslate(out.childSurfaceOffset.x, out.childSurfaceOffset.y);

  out.childWorldRect = childWorldRect;
  out.width = static_cast<int>(childSurfaceRect.width());
  out.height = static_cast<int>(childSurfaceRect.height());
  out.valid = out.width > 0 && out.height > 0;
  return out;
}

// ─── Surface-backed variant ───────────────────────────────────────────────────────────────────

class SurfaceBackgroundSource : public BackgroundSource {
 public:
  SurfaceBackgroundSource(std::shared_ptr<Surface> ownedSurface, const Matrix& imageMatrix,
                          const Rect& rect, float surfaceScale,
                          std::shared_ptr<ColorSpace> colorSpace)
      : BackgroundSource(imageMatrix, rect, surfaceScale, std::move(colorSpace)),
        ownedSurface(std::move(ownedSurface)) {
    surface = this->ownedSurface.get();
  }

  // Sub variant references a caller-owned Surface and inherits the parent's surfaceScale, so
  // BackgroundCapturer's `contentScale /= bgSource->surfaceScale()` yields the consumer-expected
  // density regardless of nesting depth.
  SurfaceBackgroundSource(Surface* borrowedSurface, const Matrix& imageMatrix, const Rect& rect,
                          float surfaceScale, std::shared_ptr<ColorSpace> colorSpace)
      : BackgroundSource(imageMatrix, rect, surfaceScale, std::move(colorSpace)),
        surface(borrowedSurface) {
  }

  Canvas* getCanvas() override {
    return surface->getCanvas();
  }

 protected:
  std::shared_ptr<Image> onGetOwnContents() override {
    return surface->makeImageSnapshot();
  }

 private:
  std::shared_ptr<Surface> ownedSurface = nullptr;  // null for sub sources
  Surface* surface = nullptr;
};

// ─── Picture-backed variant ───────────────────────────────────────────────────────────────────

class PictureBackgroundSource : public BackgroundSource {
 public:
  PictureBackgroundSource(const Matrix& surfaceMatrix, const Matrix& imageMatrix, const Rect& rect,
                          int width, int height, float surfaceScale,
                          std::shared_ptr<ColorSpace> colorSpace)
      : BackgroundSource(imageMatrix, rect, surfaceScale, std::move(colorSpace)), pixelWidth(width),
        pixelHeight(height) {
    recorder = &ownedRecorder;
    auto* canvas = recorder->beginRecording();
    canvas->setMatrix(surfaceMatrix);
  }

  // Sub variant references a caller-owned PictureRecorder. ownOriginOffset is the layer slice's
  // top-left in carrier-surface-pixel coords; readback translates the image origin by it so
  // image-pixel (0,0) aligns with the slice rather than the carrier's (0,0).
  PictureBackgroundSource(PictureRecorder* borrowedRecorder, const Matrix& imageMatrix,
                          const Rect& rect, int width, int height, float surfaceScale,
                          std::shared_ptr<ColorSpace> colorSpace, Point ownOriginOffset)
      : BackgroundSource(imageMatrix, rect, surfaceScale, std::move(colorSpace)), pixelWidth(width),
        pixelHeight(height), recorder(borrowedRecorder), ownImageOriginOffset(ownOriginOffset) {
    DEBUG_ASSERT(recorder->getRecordingCanvas() != nullptr);
  }

  Canvas* getCanvas() override {
    return recorder->getRecordingCanvas();
  }

 protected:
  std::shared_ptr<Image> onGetOwnContents() override {
    auto* oldCanvas = recorder->getRecordingCanvas();
    if (oldCanvas == nullptr) {
      return nullptr;
    }
    // Capture the live canvas state before finishing; Canvas::drawPicture saves/restores around
    // playback and does NOT carry source canvas state across, so we restore manually.
    auto savedMatrix = oldCanvas->getMatrix();
    auto savedClip = oldCanvas->getTotalClip();
    auto savedSaveCount = oldCanvas->getSaveCount();
    DEBUG_ASSERT(savedSaveCount >= 0);

    auto segment = recorder->finishRecordingAsPicture();

    auto* resumed = recorder->beginRecording();
    // Replay under identity baseline. PlaybackContext postConcats the outer matrix onto each
    // recorded SetMatrix, so any non-identity baseline would double the carrier's matrix.
    resumed->resetMatrix();
    if (segment != nullptr) {
      resumed->drawPicture(segment);
    }
    // Rebuild the caller's save depth, then install matrix + clip at the top frame. The outer
    // AutoCanvasRestore pops back to its saveCount and discards the empty saves.
    //
    // LIMITATION: only the TOP save-stack frame is preserved across this flush. Canvas's public
    // API exposes only the current matrix, total clip, and save count — there is no per-frame
    // accessor. A partial Canvas::restore() between this readback and the outer
    // AutoCanvasRestore would land on identity / wide-open clip instead of the caller's frame
    // N-1 state. Current callers (BackgroundCapturer + LayerStyle pipeline) do not partial-
    // restore across getBackgroundImage(); future callers wanting partial restores must extend
    // this resume path or expose per-frame state on Canvas.
    for (int i = 0; i < savedSaveCount; ++i) {
      resumed->save();
    }
    // savedClip is in device coords (Canvas::clipPath stores `path.transform(_matrix)` before
    // merging). Apply it under identity first, then install savedMatrix, otherwise savedMatrix
    // would re-multiply onto an already-transformed clip.
    if (!savedClip.isEmpty()) {
      resumed->clipPath(savedClip);
    }
    resumed->setMatrix(savedMatrix);

    if (segment == nullptr) {
      return nullptr;
    }
    if (ownImageOriginOffset.x == 0.0f && ownImageOriginOffset.y == 0.0f) {
      return Image::MakeFrom(std::move(segment), pixelWidth, pixelHeight);
    }
    auto matrix = Matrix::MakeTrans(-ownImageOriginOffset.x, -ownImageOriginOffset.y);
    return Image::MakeFrom(std::move(segment), pixelWidth, pixelHeight, &matrix);
  }

 private:
  int pixelWidth = 0;
  int pixelHeight = 0;
  PictureRecorder ownedRecorder;
  PictureRecorder* recorder = nullptr;  // points at ownedRecorder or a borrowed one
  // Layer slice top-left in the borrowed recorder's picture coords. Zero for top-level; non-zero
  // for sub when the layer is offset within the carrier.
  Point ownImageOriginOffset = Point::Zero();
};

}  // namespace

// ─── Shared factory ───────────────────────────────────────────────────────────────────────────

std::shared_ptr<BackgroundSource> BackgroundSource::Make(Context* context, const Rect& drawRect,
                                                         float maxOutset, float minOutset,
                                                         const Matrix& matrix,
                                                         std::shared_ptr<ColorSpace> colorSpace) {
  auto geometry = ComputeTopLevelGeometry(drawRect, maxOutset, minOutset, matrix);
  if (!geometry.valid) {
    return nullptr;
  }
  if (context != nullptr) {
    auto surface =
        Surface::Make(context, geometry.width, geometry.height, false, 1, false, 0, colorSpace);
    if (!surface) {
      return nullptr;
    }
    auto result = std::make_shared<SurfaceBackgroundSource>(
        std::move(surface), geometry.imageMatrix, geometry.imageRect, geometry.surfaceScale,
        colorSpace);
    auto* canvas = result->getCanvas();
    canvas->clear();
    canvas->setMatrix(geometry.surfaceMatrix);
    return result;
  }
  return std::make_shared<PictureBackgroundSource>(
      geometry.surfaceMatrix, geometry.imageMatrix, geometry.imageRect, geometry.width,
      geometry.height, geometry.surfaceScale, std::move(colorSpace));
}

// ─── Shared parent-merge logic ────────────────────────────────────────────────────────────────

std::shared_ptr<Image> BackgroundSource::getBackgroundImage() {
  auto ownImage = onGetOwnContents();
  if (parent == nullptr) {
    // Top-level: own contents == full background image.
    return ownImage;
  }

  // Sub-source: compose parent backdrop with own contents in sub image-pixel space. ownImage is
  // in sub-surface-pixel space and must be warped via: sub surface → world → parent image → sub
  // image (translate by -surfaceOffset). Parent image may legitimately be absent (e.g. top
  // picture-backed source not yet flushed) or sub may fall outside the parent footprint; in
  // both cases parent is treated as transparent and only own contents contribute.
  Matrix worldToParentImage = Matrix::I();
  if (!parent->imageMatrix.invert(&worldToParentImage)) {
    DEBUG_ASSERT(false);
    return nullptr;
  }
  int width = FloatCeilToInt(backgroundRect.width() * worldToParentImage.getMaxScale());
  int height = FloatCeilToInt(backgroundRect.height() * worldToParentImage.getMaxScale());
  if (width <= 0 || height <= 0) {
    return nullptr;
  }

  Matrix ownToSubImage = worldToParentImage;
  ownToSubImage.preConcat(surfaceToWorld);
  ownToSubImage.postTranslate(-surfaceOffset.x, -surfaceOffset.y);

  std::shared_ptr<Image> subsetImage = nullptr;
  float subsetOffsetX = 0.0f;
  float subsetOffsetY = 0.0f;
  if (auto parentImage = parent->getBackgroundImage()) {
    auto subsetRect = Rect::MakeXYWH(surfaceOffset.x, surfaceOffset.y, static_cast<float>(width),
                                     static_cast<float>(height));
    auto parentBounds = Rect::MakeWH(parentImage->width(), parentImage->height());
    auto validRect = subsetRect;
    if (validRect.intersect(parentBounds)) {
      subsetOffsetX = validRect.left - subsetRect.left;
      subsetOffsetY = validRect.top - subsetRect.top;
      subsetImage = parentImage->makeSubset(validRect);
      DEBUG_ASSERT(subsetImage != nullptr);
    }
  }

  if (subsetImage == nullptr && ownImage == nullptr) {
    return nullptr;
  }

  PictureRecorder recorder;
  auto canvas = recorder.beginRecording();
  if (subsetImage) {
    canvas->drawImage(subsetImage, subsetOffsetX, subsetOffsetY);
  }
  if (ownImage) {
    canvas->concat(ownToSubImage);
    canvas->drawImage(ownImage);
  }
  auto picture = recorder.finishRecordingAsPicture();
  if (picture == nullptr) {
    DEBUG_ASSERT(false);
    return nullptr;
  }
  return Image::MakeFrom(std::move(picture), width, height);
}

// ─── Sub-source factories ─────────────────────────────────────────────────────────────────────

std::shared_ptr<BackgroundSource> BackgroundSource::createFromSurface(
    Surface* subSurface, const Rect& renderBounds, const Matrix& localToWorld,
    const Matrix& localToSurface) {
  if (subSurface == nullptr) {
    return nullptr;
  }
  auto geometry = ComputeSubGeometry(imageMatrix, renderBounds, localToWorld, localToSurface);
  if (!geometry.valid) {
    return nullptr;
  }
  // The borrowed surface's pixel (0,0) is at localToSurface^{-1}(0,0) in local space.
  Matrix surfaceToLocal = Matrix::I();
  if (!localToSurface.invert(&surfaceToLocal)) {
    return nullptr;
  }
  auto sub = std::make_shared<SurfaceBackgroundSource>(
      subSurface, geometry.childImageMatrix, geometry.childWorldRect, _surfaceScale, colorSpace);
  sub->parent = this;
  sub->surfaceOffset = geometry.childSurfaceOffset;
  sub->surfaceToWorld = localToWorld;
  sub->surfaceToWorld.preConcat(surfaceToLocal);
  return sub;
}

std::shared_ptr<BackgroundSource> BackgroundSource::createFromPicture(
    PictureRecorder* subRecorder, const Rect& renderBounds, const Matrix& localToWorld,
    const Matrix& localToSurface) {
  if (subRecorder == nullptr || subRecorder->getRecordingCanvas() == nullptr) {
    return nullptr;
  }
  auto geometry = ComputeSubGeometry(imageMatrix, renderBounds, localToWorld, localToSurface);
  if (!geometry.valid) {
    return nullptr;
  }
  auto sub = std::make_shared<PictureBackgroundSource>(
      subRecorder, geometry.childImageMatrix, geometry.childWorldRect, geometry.width,
      geometry.height, _surfaceScale, colorSpace, geometry.childSurfaceTopLeft);
  sub->parent = this;
  sub->surfaceOffset = geometry.childSurfaceOffset;
  Matrix surfaceToLocal = Matrix::I();
  if (!localToSurface.invert(&surfaceToLocal)) {
    return nullptr;
  }
  sub->surfaceToWorld = localToWorld;
  sub->surfaceToWorld.preConcat(surfaceToLocal);
  return sub;
}

}  // namespace tgfx
