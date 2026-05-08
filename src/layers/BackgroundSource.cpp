/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

// Geometry shared by top-level Make of both Surface and Picture variants. Produces the two
// matrices (world → surface pixel, and image → world) plus the surface/image dimensions.
struct TopLevelGeometry {
  Matrix surfaceMatrix = Matrix::I();  // world → surface pixel
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
  // Scale-down when extreme blur outsets would balloon the surface past the single-pass blur
  // budget. Applied uniformly to both axes so the sub image pixel grid remains isotropic.
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

// Geometry shared by createSubSurface() / createSubPicture() of both variants.
// `localToWorld`/`localToSurface` describe how the sub's local coords map to world and to the sub
// surface's pixel grid; the parent's imageMatrix is used to compute the sub's image-pixel space
// relative to the parent.
struct SubGeometry {
  Matrix childSurfaceMatrix = Matrix::I();   // local → sub surface pixel
  Matrix childSurfaceToWorld = Matrix::I();  // sub surface pixel → world
  Matrix childImageMatrix = Matrix::I();     // sub image pixel → world
  Rect childWorldRect = Rect::MakeEmpty();   // sub's bounds in world coords
  Point childSurfaceOffset = Point::Zero();  // sub surface origin in parent image-pixel coords
  // Top-left of childSurfaceRect in carrier-surface-pixel coords (i.e. in the borrowed
  // PictureRecorder's drawing space). Differs from childSurfaceOffset, which is in parent
  // image-pixel coords. Used by the picture sub variant to translate its image origin to the
  // slice the layer actually drew into.
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

  out.childSurfaceMatrix = localToSurface;
  out.childSurfaceMatrix.postTranslate(-childSurfaceRect.x(), -childSurfaceRect.y());
  out.childSurfaceTopLeft = Point::Make(childSurfaceRect.x(), childSurfaceRect.y());

  // childSurfaceToWorld: sub surface pixel → local → world.
  Matrix childSurfaceToLocal = Matrix::I();
  if (!out.childSurfaceMatrix.invert(&childSurfaceToLocal)) {
    return out;
  }
  out.childSurfaceToWorld = localToWorld;
  out.childSurfaceToWorld.preConcat(childSurfaceToLocal);

  // surfaceOffset is in parent image-pixel space — the subset space getBackgroundImage() uses.
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
  // Top-level: owns a freshly allocated Surface.
  SurfaceBackgroundSource(std::shared_ptr<Surface> ownedSurface, const Matrix& imageMatrix,
                          const Rect& rect, float surfaceScale,
                          std::shared_ptr<ColorSpace> colorSpace)
      : BackgroundSource(imageMatrix, rect, surfaceScale, std::move(colorSpace)),
        ownedSurface(std::move(ownedSurface)) {
    surface = this->ownedSurface.get();
  }

  // Sub: references a caller-owned Surface (e.g. the offscreen carrier's surface). Inherits the
  // parent's surfaceScale so BackgroundCapturer's `contentScale /= bgSource->surfaceScale()`
  // normalisation produces the consumer-expected density regardless of nesting depth. Sub does
  // not introduce its own down-sampling: its canvas matrix already carries every ancestor's
  // density, and any top-level down-sample factor (for extreme blur outsets) must still be
  // divided out when consumers sample this sub's snapshot.
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
  // Top-level: owns a PictureRecorder.
  PictureBackgroundSource(const Matrix& surfaceMatrix, const Matrix& imageMatrix, const Rect& rect,
                          int width, int height, float surfaceScale,
                          std::shared_ptr<ColorSpace> colorSpace)
      : BackgroundSource(imageMatrix, rect, surfaceScale, std::move(colorSpace)), pixelWidth(width),
        pixelHeight(height) {
    recorder = &ownedRecorder;
    auto* canvas = recorder->beginRecording();
    canvas->setMatrix(surfaceMatrix);
  }

  // Sub: references a caller-owned PictureRecorder. Inherits the parent's surfaceScale for the
  // same reason as SurfaceBackgroundSource — see that constructor for the rationale.
  // ownOriginOffset is childSurfaceRect.topLeft in carrier-surface-pixel coords; the borrowed
  // recorder's canvas matrix is the parent localToSurface (without the postTranslate that aligns
  // a dedicated sub surface to its own origin), so picture-(0,0) corresponds to carrier-(0,0)
  // rather than the slice the layer drew to. We keep this offset and translate the image origin
  // at readback.
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
    // Capture the caller's live canvas state before finishing the segment, so we can restore it
    // verbatim on the new recording. Canvas::drawPicture is documented to save/restore around the
    // playback, so it does NOT carry the source canvas state across — we have to do it manually.
    auto savedMatrix = oldCanvas->getMatrix();
    auto savedClip = oldCanvas->getTotalClip();
    auto savedSaveCount = oldCanvas->getSaveCount();

    auto segment = recorder->finishRecordingAsPicture();

    auto* resumed = recorder->beginRecording();
    // Replay prior content under an identity baseline. PlaybackContext composes the outer canvas
    // matrix onto each recorded SetMatrix op (postConcat), so any non-identity baseline would
    // double the carrier's matrix during replay. An identity baseline lets segment's own
    // SetMatrix ops install the exact absolute matrices they recorded.
    resumed->resetMatrix();
    if (segment != nullptr) {
      resumed->drawPicture(segment);
    }
    // Rebuild the caller's save depth by pushing empty saves, then install their top-level
    // matrix + total clip at the top save level. Any outer AutoCanvasRestore will pop back to
    // its remembered saveCount, discarding the empty saves and leaving the canvas at the pre-
    // capture baseline — exactly the same lifecycle as before the flush.
    //
    // LIMITATION: only the TOP save-stack frame is preserved across this flush. Intermediate
    // frames (matrix/clip per save level) are lost because Canvas's public API exposes only the
    // current matrix, total clip, and save count — there is no way to enumerate per-frame state.
    // A partial Canvas::restore() between this readback and the outer AutoCanvasRestore would
    // therefore land on identity matrix / wide-open clip (the empty saves' baseline) instead of
    // the caller's frame N-1 state, producing wrong output.
    //
    // Current callers (BackgroundCapturer + LayerStyle pipeline) do not partial-restore across
    // getBackgroundImage(); they wrap the whole readback in an outer AutoCanvasRestore that pops
    // straight back to the pre-capture saveCount. Future callers wanting partial restores must
    // either re-architect this resume path or extend Canvas with a public per-frame accessor.
    for (int i = 0; i < savedSaveCount; ++i) {
      resumed->save();
    }
    // savedClip is in device coords because Canvas::clipPath stores `path.transform(_matrix)`
    // before merging — i.e. getTotalClip() is already pre-multiplied by the source matrix. Apply
    // it under identity first, then install savedMatrix, so subsequent clipPath calls go through
    // savedMatrix as expected. Reversing this order would multiply savedMatrix onto an already-
    // transformed clip, producing a double-transform.
    if (!savedClip.isEmpty()) {
      resumed->clipPath(savedClip);
    }
    resumed->setMatrix(savedMatrix);

    if (segment == nullptr) {
      return nullptr;
    }
    // PictureImage's matrix maps picture-coords → image-pixel space. For top-level the borrowed
    // recorder's canvas already records into picture-(0,0)..picture-(pixelWidth, pixelHeight),
    // so a null matrix (identity) is correct. For sub the borrowed recorder records the layer
    // at picture-(childSurfaceRect.topLeft + (0..w, 0..h)), so we must translate by the negative
    // top-left to make image-pixel (0,0) sample picture-(childSurfaceRect.x, childSurfaceRect.y).
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
  // Top-left of the layer's slice in the borrowed recorder's picture coords. Zero for top-level
  // (image origin already aligns with picture origin) and non-zero for sub when the layer is
  // offset within the carrier.
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

  // Sub-source fallbacks below normally return nullptr rather than ownImage. Reason: ownImage is
  // in sub-surface-pixel space, while consumers interpret getBackgroundImage()'s result through
  // backgroundMatrix(), which maps parent-image-pixel → world. Returning ownImage on a fallback
  // would feed the consumer pixels in the wrong coord space when a sub introduces its own density
  // relative to the parent image. Exception: when the sub did NOT introduce its own density
  // (imageMatrix == surfaceToWorld), sub-surface-pixel and sub-image-pixel coincide and ownImage
  // is consumable directly — drop it only when the spaces actually differ. The capturer
  // (BackgroundCapturer::drawBackgroundStyle) null-checks the result and skips the snapshot when
  // null, so returning nullptr degrades gracefully instead of producing wrongly-sampled output.
  auto subFallback = (imageMatrix == surfaceToWorld) ? ownImage : nullptr;
  auto parentImage = parent->getBackgroundImage();
  if (parentImage == nullptr) {
    return subFallback;
  }

  // Sub: compose parent image (in parent image-pixel coords) with own content by subsetting
  // parent to the sub's footprint and stacking own on top. Sub's image-pixel space is aligned
  // with parent's, offset by surfaceOffset (set at createSubSurface / createSubPicture time).
  Matrix worldToParentImage = Matrix::I();
  if (!parent->imageMatrix.invert(&worldToParentImage)) {
    return subFallback;
  }
  int width = FloatCeilToInt(backgroundRect.width() * worldToParentImage.getMaxScale());
  int height = FloatCeilToInt(backgroundRect.height() * worldToParentImage.getMaxScale());
  if (width <= 0 || height <= 0) {
    return subFallback;
  }

  auto subsetRect = Rect::MakeXYWH(surfaceOffset.x, surfaceOffset.y, static_cast<float>(width),
                                   static_cast<float>(height));
  auto parentBounds = Rect::MakeWH(parentImage->width(), parentImage->height());
  auto validRect = subsetRect;
  if (!validRect.intersect(parentBounds)) {
    return subFallback;
  }
  auto childOffsetX = validRect.left - subsetRect.left;
  auto childOffsetY = validRect.top - subsetRect.top;
  auto subsetImage = parentImage->makeSubset(validRect);
  if (!subsetImage) {
    return subFallback;
  }

  // Warp own (in sub surface pixel) to sub image pixel: surface → world → parent image → sub image.
  Matrix ownToSubImage = worldToParentImage;
  ownToSubImage.preConcat(surfaceToWorld);
  ownToSubImage.postTranslate(-surfaceOffset.x, -surfaceOffset.y);

  PictureRecorder recorder;
  auto canvas = recorder.beginRecording();
  canvas->drawImage(subsetImage, childOffsetX, childOffsetY);
  if (ownImage) {
    canvas->concat(ownToSubImage);
    canvas->drawImage(ownImage);
  }
  auto picture = recorder.finishRecordingAsPicture();
  if (picture == nullptr) {
    return subFallback;
  }
  return Image::MakeFrom(std::move(picture), width, height);
}

// ─── Sub-source factories ─────────────────────────────────────────────────────────────────────

std::shared_ptr<BackgroundSource> BackgroundSource::createSubSurface(Surface* subSurface,
                                                                     const Rect& renderBounds,
                                                                     const Matrix& localToWorld,
                                                                     const Matrix& localToSurface) {
  if (subSurface == nullptr) {
    return nullptr;
  }
  auto geometry = ComputeSubGeometry(imageMatrix, renderBounds, localToWorld, localToSurface);
  if (!geometry.valid) {
    return nullptr;
  }
  // For a borrowed surface, pixel (0,0) of the carrier corresponds to localToSurface^{-1}(0,0) in
  // local space. Compute surfaceToWorld from localToWorld and localToSurface directly rather than
  // using geometry.childSurfaceToWorld, which assumes a dedicated sub surface whose origin aligns
  // with childSurfaceRect.topLeft. The Picture variant does not share this issue because its image
  // is explicitly sized to childSurfaceRect dimensions with a matching origin.
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

std::shared_ptr<BackgroundSource> BackgroundSource::createSubPicture(PictureRecorder* subRecorder,
                                                                     const Rect& renderBounds,
                                                                     const Matrix& localToWorld,
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
  // Compute surfaceToWorld from localToWorld and localToSurface directly, same as createSubSurface.
  // geometry.childSurfaceToWorld assumes a dedicated sub surface whose origin aligns with
  // childSurfaceRect.topLeft, but the borrowed PictureRecorder's canvas matrix is localToSurface
  // (without the postTranslate to childSurfaceRect.topLeft), so we must use the runtime canvas
  // chain instead.
  Matrix surfaceToLocal = Matrix::I();
  if (!localToSurface.invert(&surfaceToLocal)) {
    return nullptr;
  }
  sub->surfaceToWorld = localToWorld;
  sub->surfaceToWorld.preConcat(surfaceToLocal);
  return sub;
}

}  // namespace tgfx
