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

#pragma once
#include "tgfx/core/PictureRecorder.h"
#include "tgfx/core/Surface.h"

namespace tgfx {

/**
 * BackgroundSource is a read-only query handle that lets Background-sourced layer styles
 * sample the already-rendered backdrop during the capture pass. It is *not* a drawing
 * destination on its own — callers render into `getCanvas()` (which points at either a GPU
 * Surface canvas or a PictureRecorder recording canvas) and later call `getBackgroundImage()`
 * to obtain a read-back Image covering "parent backdrop + everything drawn through getCanvas()
 * so far".
 *
 * Two concrete variants exist:
 *   - SurfaceBackgroundSource: backed by a real GPU Surface; readback is a cheap snapshot.
 *     Used whenever a GPU Context is available.
 *   - PictureBackgroundSource: backed by a PictureRecorder; readback finishes the current
 *     segment, flattens it into an Image, then transparently re-opens a new recording segment
 *     with the prior canvas state (matrix / clip / saveCount) restored. Used when no GPU
 *     Context is available (e.g. PDF / SVG export, or picture-only render paths).
 */
class BackgroundSource {
 public:
  /**
   * Creates the top-level BackgroundSource for a render pass. When `context` is non-null the
   * result is a SurfaceBackgroundSource; when `context` is null the result is a
   * PictureBackgroundSource and all subsequent rendering happens into picture recorders.
   */
  static std::shared_ptr<BackgroundSource> Make(Context* context, const Rect& drawRect,
                                                float maxOutset, float minOutset,
                                                const Matrix& matrix,
                                                std::shared_ptr<ColorSpace> colorSpace);

  virtual ~BackgroundSource() = default;

  /**
   * Returns the canvas callers draw into to contribute pixels to this BackgroundSource. The
   * returned pointer is stable across getBackgroundImage() calls (PictureRecorder reuses the
   * canvas object across finish / begin cycles), so callers may cache it safely for the lifetime
   * of this BackgroundSource.
   */
  virtual Canvas* getCanvas() = 0;

  /**
   * Returns the matrix mapping image-pixel coordinates (of the image returned by
   * getBackgroundImage) to world coordinates.
   */
  Matrix backgroundMatrix() const {
    return imageMatrix;
  }

  /**
   * Returns an Image that represents "parent backdrop + everything drawn through getCanvas() so
   * far", in this source's image-pixel coordinate space. Safe to call repeatedly — in the
   * Picture-backed variant each call flattens a new segment, then transparently re-opens a new
   * recording segment with the prior canvas state restored.
   */
  std::shared_ptr<Image> getBackgroundImage();

  /**
   * Derives a sub BackgroundSource whose backing store is an externally-owned Surface (belonging
   * to the caller's offscreen carrier). Draws done through the sub's getCanvas() go to that same
   * Surface. `renderBounds` is in world coords; `localToWorld` / `localToSurface` describe how
   * the sub's local coord system maps to world and to the sub surface's pixel grid respectively.
   */
  std::shared_ptr<BackgroundSource> createSubSurface(Surface* subSurface, const Rect& renderBounds,
                                                     const Matrix& localToWorld,
                                                     const Matrix& localToSurface);

  /**
   * Derives a sub BackgroundSource whose backing store is an externally-owned PictureRecorder.
   * Draws done through the sub's getCanvas() go to `subRecorder`'s recording canvas. The caller
   * must keep `subRecorder` alive for the lifetime of the returned sub source; getCanvas() on
   * the sub transparently re-queries the recorder so canvas swaps (from segment flushes on
   * getBackgroundImage) are absorbed.
   */
  std::shared_ptr<BackgroundSource> createSubPicture(PictureRecorder* subRecorder,
                                                     const Rect& renderBounds,
                                                     const Matrix& localToWorld,
                                                     const Matrix& localToSurface);

  /**
   * Returns the matrix mapping the physical surface-pixel coordinates of this source (the pixel
   * grid of the image returned by onGetOwnContents — the bg surface for top-level, or the borrowed
   * offscreen carrier for subs) to world coordinates. Coincides with backgroundMatrix() for
   * top-level sources; differs for subs whose surface pixel grid does not match the parent-aligned
   * image pixel grid. Callers mapping layer-local coords through a capture canvas (whose matrix is
   * `local → this source's surface pixel`) compose with this matrix to reach world.
   */
  Matrix surfaceToWorldMatrix() const {
    return surfaceToWorld;
  }

  /**
   * Returns the physical surface-pixel downsampling factor (<=1) applied when the top-level
   * source was created for extreme blur outsets. The capture pass needs this to align the
   * snapshot's contentScale with the consume pass — because the capture pass walks the layer
   * tree on the bg canvas (whose matrix.maxScale = world-scale * surfaceScale) while the
   * consume pass walks on the caller's canvas (whose matrix.maxScale = world-scale). Sub
   * sources return 1 because sub surfaces are sized to local coordinates without the down-scale.
   */
  float surfaceScale() const {
    return _surfaceScale;
  }

 protected:
  BackgroundSource(const Matrix& imageMatrix, const Rect& rect, float surfaceScale,
                   std::shared_ptr<ColorSpace> colorSpace)
      : imageMatrix(imageMatrix), surfaceToWorld(imageMatrix), backgroundRect(rect),
        _surfaceScale(surfaceScale), colorSpace(std::move(colorSpace)) {
  }

  // Subclasses return an Image representing only *this* source's own accumulated contents (no
  // parent merge). The shared getBackgroundImage() composes it with `parent->getBackgroundImage`.
  virtual std::shared_ptr<Image> onGetOwnContents() = 0;

  // `imageMatrix` maps image pixel (what getBackgroundImage returns) → world.
  Matrix imageMatrix = Matrix::I();
  // `surfaceToWorld` maps physical surface pixel (what onGetOwnContents returns) → world. Equals
  // imageMatrix for top-level sources and for 2D sub sources where surface and image share the
  // same pixel grid; kept as a separate field so the parent-merge step can warp a sub's own
  // content into the sub's image-pixel space without assuming the two coincide.
  Matrix surfaceToWorld = Matrix::I();
  // Axis-aligned bounding box covered by this source, in image-pixel space.
  Rect backgroundRect = Rect::MakeEmpty();
  // 1.0 for sub sources; <= 1.0 for top-level sources that had to down-scale to fit extreme
  // blur outsets into a single-pass blur budget.
  float _surfaceScale = 1.0f;
  std::shared_ptr<ColorSpace> colorSpace = nullptr;

  BackgroundSource* parent = nullptr;
  // Offset of this source's surface origin in parent image-pixel coords. Populated by
  // createSubSurface() / createSubPicture() for sub sources.
  Point surfaceOffset = Point::Zero();
};

}  // namespace tgfx
