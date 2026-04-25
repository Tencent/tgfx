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
 * Read-only query handle that lets Background-sourced LayerStyles sample the already-rendered
 * backdrop. Callers render into getCanvas() and later call getBackgroundImage() to read it back.
 * Two variants: SurfaceBackgroundSource (GPU, cheap snapshot) and PictureBackgroundSource
 * (no GPU context, re-records segments on readback — used for PDF / SVG / picture-only paths).
 */
class BackgroundSource {
 public:
  // Creates the top-level source. Context null → PictureBackgroundSource.
  static std::shared_ptr<BackgroundSource> Make(Context* context, const Rect& drawRect,
                                                float maxOutset, float minOutset,
                                                const Matrix& matrix,
                                                std::shared_ptr<ColorSpace> colorSpace);

  virtual ~BackgroundSource() = default;

  // Canvas callers draw into. Pointer is stable across getBackgroundImage() calls.
  virtual Canvas* getCanvas() = 0;

  // image pixel → world, where image pixel is the coord space of getBackgroundImage()'s result.
  Matrix backgroundMatrix() const {
    return imageMatrix;
  }

  // Returns "parent backdrop + everything drawn through getCanvas() so far". Safe to call
  // repeatedly — the Picture variant flushes and reopens a segment each time.
  std::shared_ptr<Image> getBackgroundImage();

  // Derives a sub source backed by an externally-owned Surface (the offscreen carrier's surface).
  // renderBounds is in world coords; localToWorld / localToSurface map the sub's local coords.
  std::shared_ptr<BackgroundSource> createSubSurface(Surface* subSurface, const Rect& renderBounds,
                                                     const Matrix& localToWorld,
                                                     const Matrix& localToSurface);

  // Derives a sub source backed by an externally-owned PictureRecorder. Caller must keep the
  // recorder alive for the sub's lifetime.
  std::shared_ptr<BackgroundSource> createSubPicture(PictureRecorder* subRecorder,
                                                     const Rect& renderBounds,
                                                     const Matrix& localToWorld,
                                                     const Matrix& localToSurface);

  // surface pixel → world. Same as backgroundMatrix() for top-level; differs for subs whose
  // surface pixel grid doesn't match the parent-aligned image grid. Used when composing with a
  // capture canvas whose matrix targets surface pixels.
  Matrix surfaceToWorldMatrix() const {
    return surfaceToWorld;
  }

  // The world-space rect this source's surface covers (includes blur outset).
  Rect getBackgroundRect() const {
    return backgroundRect;
  }

  // Down-sample factor (<=1) applied to the top-level bg surface for extreme blur outsets. Sub
  // sources inherit the top-level's value so capturers can divide it out uniformly — the sub's
  // own canvas already carries every ancestor's density, but the top-level down-sample factor is
  // the only scale a consumer's render canvas does NOT see, so it must be removed at capture.
  float surfaceScale() const {
    return _surfaceScale;
  }

 protected:
  BackgroundSource(const Matrix& imageMatrix, const Rect& rect, float surfaceScale,
                   std::shared_ptr<ColorSpace> colorSpace)
      : imageMatrix(imageMatrix), surfaceToWorld(imageMatrix), backgroundRect(rect),
        _surfaceScale(surfaceScale), colorSpace(std::move(colorSpace)) {
  }

  // Subclass hook: returns an Image of only this source's own contents. The shared
  // getBackgroundImage() composes it with parent->getBackgroundImage() when a parent exists.
  virtual std::shared_ptr<Image> onGetOwnContents() = 0;

  Matrix imageMatrix = Matrix::I();     // image pixel → world
  Matrix surfaceToWorld = Matrix::I();  // surface pixel → world (== imageMatrix for top-level)
  Rect backgroundRect = Rect::MakeEmpty();
  float _surfaceScale = 1.0f;
  std::shared_ptr<ColorSpace> colorSpace = nullptr;

  BackgroundSource* parent = nullptr;
  // Origin of this source's surface in parent image-pixel coords. Set by createSub* for subs.
  Point surfaceOffset = Point::Zero();
};

}  // namespace tgfx
