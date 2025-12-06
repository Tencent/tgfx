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

#include "BackgroundContext.h"
#include <utility>
#include "core/filters/GaussianBlurImageFilter.h"
#include "tgfx/core/PictureRecorder.h"

namespace tgfx {

class SurfaceBackgroundContext : public BackgroundContext {
 public:
  static std::shared_ptr<SurfaceBackgroundContext> Make(Context* context, const Matrix& matrix,
                                                        const Rect& rect,
                                                        std::shared_ptr<ColorSpace> colorSpace) {
    auto invertMatrix = Matrix::I();
    matrix.invert(&invertMatrix);
    auto surfaceRect = invertMatrix.mapRect(rect);
    auto surface =
        Surface::Make(context, static_cast<int>(surfaceRect.width()),
                      static_cast<int>(surfaceRect.height()), false, 1, false, 0, colorSpace);
    if (!surface) {
      return nullptr;
    }
    return std::shared_ptr<SurfaceBackgroundContext>(
        new SurfaceBackgroundContext(surface, matrix, rect, std::move(colorSpace)));
  }
  Canvas* getCanvas() override {
    return surface->getCanvas();
  }
  std::shared_ptr<Image> onGetBackgroundImage() override;

 private:
  SurfaceBackgroundContext(std::shared_ptr<Surface> surface, const Matrix& matrix, const Rect& rect,
                           std::shared_ptr<ColorSpace> colorSpace)
      : BackgroundContext(surface->getContext(), matrix, rect, std::move(colorSpace)),
        surface(std::move(surface)) {
  }
  std::shared_ptr<Surface> surface = nullptr;
};

std::shared_ptr<Image> SurfaceBackgroundContext::onGetBackgroundImage() {
  return surface->makeImageSnapshot();
}

static float MaxBlurOutset() {
  static float MaxOutset = 0;
  if (MaxOutset != 0) {
    return MaxOutset;
  }
  auto filter =
      ImageFilter::Blur(GaussianBlurImageFilter::MaxSigma(), GaussianBlurImageFilter::MaxSigma());
  MaxOutset = filter->filterBounds(Rect::MakeEmpty()).right;
  return MaxOutset;
}

std::shared_ptr<BackgroundContext> BackgroundContext::Make(Context* context, const Rect& drawRect,
                                                           float maxOutset, float minOutset,
                                                           const Matrix& matrix,
                                                           std::shared_ptr<ColorSpace> colorSpace) {
  if (!context) {
    return nullptr;
  }
  auto surfaceScale = 1.0f;
  auto rect = drawRect;
  rect.outset(maxOutset, maxOutset);
  rect.roundOut();
  auto maxBlurOutset = MaxBlurOutset();
  if (minOutset > maxBlurOutset) {
    surfaceScale = maxBlurOutset / minOutset;
  }
  rect.scale(surfaceScale, surfaceScale);
  rect.roundOut();
  auto surfaceMatrix = Matrix::MakeTrans(-rect.x(), -rect.y());
  surfaceMatrix.preScale(surfaceScale, surfaceScale);
  surfaceMatrix.preConcat(matrix);
  Matrix imageMatrix = Matrix::I();
  if (!surfaceMatrix.invert(&imageMatrix)) {
    return nullptr;
  }
  auto backgroundRect = Rect::MakeWH(rect.width(), rect.height());
  imageMatrix.mapRect(&backgroundRect);
  std::shared_ptr<BackgroundContext> result =
      SurfaceBackgroundContext::Make(context, imageMatrix, backgroundRect, std::move(colorSpace));
  if (!result) {
    return result;
  }
  auto canvas = result->getCanvas();
  canvas->clear();
  canvas->setMatrix(surfaceMatrix);
  return result;
}

void BackgroundContext::drawToParent(float contentScale, const Paint& paint) {
  if (!parent) {
    return;
  }
  auto parentCanvas = parent->getCanvas();
  AutoCanvasRestore autoRestore(parentCanvas);

  auto newPaint = paint;
  auto maskFilter = newPaint.getMaskFilter();
  if (maskFilter) {
    // The mask filter's shader has an internal matrix (affineRelativeMatrix) that transforms
    // from scaled layer local coordinates to mask image coordinates.
    //
    // When we call makeWithMatrix(M), the new matrix becomes M * affineRelativeMatrix,
    // and the actual UV transform is inverse(M * affineRelativeMatrix) = inverse(affineRelativeMatrix) * inverse(M).
    //
    // We need the final UV transform to be: inverse(affineRelativeMatrix) * maskAdjustMatrix,
    // where maskAdjustMatrix transforms child surface coord to scaled layer local coord.
    //
    // So we need: inverse(M) = maskAdjustMatrix, i.e., M = inverse(maskAdjustMatrix).
    //
    // maskAdjustMatrix = Scale(contentScale) * inverse(childCanvasMatrix)
    // inverse(maskAdjustMatrix) = childCanvasMatrix * Scale(1/contentScale)

    auto childCanvasMatrix = getCanvas()->getMatrix();

    // Build inverse(maskAdjustMatrix) = childCanvasMatrix * Scale(1/contentScale)
    auto inverseMaskAdjustMatrix = childCanvasMatrix;
    inverseMaskAdjustMatrix.preScale(1.0f / contentScale, 1.0f / contentScale);

    newPaint.setMaskFilter(maskFilter->makeWithMatrix(inverseMaskAdjustMatrix));
  }

  // Use setMatrix + drawImage(image) to draw at surfaceOffset in parent surface.
  parentCanvas->setMatrix(Matrix::MakeTrans(surfaceOffset.x, surfaceOffset.y));
  auto image = onGetBackgroundImage();
  if (image) {
    parentCanvas->drawImage(image, &newPaint);
  }
}

Matrix BackgroundContext::backgroundMatrix() const {
  return imageMatrix;
}

std::shared_ptr<Image> BackgroundContext::getBackgroundImage() {
  auto image = onGetBackgroundImage();
  DEBUG_ASSERT(image);
  if (!parent) {
    return image;
  }

  // Get parent's background image (in parent's surface coordinate system).
  auto parentImage = parent->getBackgroundImage();

  // surfaceOffset is the position of this child's surface in parent's surface coordinates.
  // We need to extract the corresponding subset from parentImage.
  int width = image->width();
  int height = image->height();

  // The subset position in parentImage is surfaceOffset (relative to parent surface origin).
  const float& subsetX = surfaceOffset.x;
  const float& subsetY = surfaceOffset.y;

  auto subsetImage = parentImage->makeSubset(
      Rect::MakeXYWH(subsetX, subsetY, static_cast<float>(width), static_cast<float>(height)));

  if (!subsetImage) {
    return image;
  }

  PictureRecorder recorder;
  auto canvas = recorder.beginRecording();
  canvas->drawImage(subsetImage);
  canvas->drawImage(image);
  auto picture = recorder.finishRecordingAsPicture();
  if (!picture) {
    return image;
  }
  return Image::MakeFrom(std::move(picture), width, height);
}

std::shared_ptr<BackgroundContext> BackgroundContext::createSubContext(const Rect& renderBounds,
                                                                       bool clipToBackgroundRect) {
  Rect childWorldRect = renderBounds;
  if (clipToBackgroundRect) {
    if (!childWorldRect.intersect(backgroundRect)) {
      return nullptr;
    }
  } else if (!Rect::Intersects(renderBounds, backgroundRect)) {
    return nullptr;
  }

  auto canvas = getCanvas();
  auto parentCanvasMatrix = canvas->getMatrix();

  // Use imageMatrix inverse (world -> surface) to calculate child surface bounds.
  Matrix baseSurfaceMatrix = Matrix::I();
  if (!imageMatrix.invert(&baseSurfaceMatrix)) {
    return nullptr;
  }

  // Map childWorldRect to parent surface coordinates using baseSurfaceMatrix.
  auto childSurfaceRect = baseSurfaceMatrix.mapRect(childWorldRect);
  childSurfaceRect.roundOut();

  // surfaceOffset: position of child surface origin in parent surface coordinates.
  auto childSurfaceOffset = Point::Make(childSurfaceRect.x(), childSurfaceRect.y());

  // Build child's surfaceMatrix: world -> child surface.
  // childSurfaceMatrix = Translate(-childSurfaceOffset) * baseSurfaceMatrix
  auto childSurfaceMatrix = baseSurfaceMatrix;
  childSurfaceMatrix.postTranslate(-childSurfaceOffset.x, -childSurfaceOffset.y);

  // Child's imageMatrix: child surface -> world.
  Matrix childImageMatrix = Matrix::I();
  if (!childSurfaceMatrix.invert(&childImageMatrix)) {
    return nullptr;
  }

  // Child's backgroundRect in world coordinates.
  // Map child surface bounds (0, 0, w, h) through childImageMatrix.
  auto childBackgroundRect = Rect::MakeWH(childSurfaceRect.width(), childSurfaceRect.height());
  childImageMatrix.mapRect(&childBackgroundRect);

  // Calculate child canvas matrix.
  // childCanvasMatrix = childSurfaceMatrix * imageMatrix * parentCanvasMatrix
  // This transforms: layer local -> parent surface -> world -> child surface
  // = layer local -> child surface
  auto childCanvasMatrix = childSurfaceMatrix;
  childCanvasMatrix.preConcat(imageMatrix);
  childCanvasMatrix.preConcat(parentCanvasMatrix);

  std::shared_ptr<BackgroundContext> child =
      SurfaceBackgroundContext::Make(context, childImageMatrix, childBackgroundRect, colorSpace);
  if (!child) {
    return nullptr;
  }

  auto childCanvas = child->getCanvas();
  childCanvas->clear();
  // Use childCanvasMatrix which inherits from parentCanvasMatrix.
  childCanvas->setMatrix(childCanvasMatrix);

  child->parent = this;
  child->surfaceOffset = childSurfaceOffset;
  return child;
}

}  // namespace tgfx
