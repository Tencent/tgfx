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
#include "core/filters/GaussianBlurImageFilter.h"
#include "tgfx/core/PictureRecorder.h"

namespace tgfx {

class PictureBackgroundContext : public BackgroundContext {
 public:
  static std::shared_ptr<BackgroundContext> Make(const Matrix& imageMatrix, const Rect& rect,
                                                 std::shared_ptr<ColorSpace> colorSpace) {
    return std::shared_ptr<PictureBackgroundContext>(
        new PictureBackgroundContext(imageMatrix, rect, std::move(colorSpace)));
  }
  ~PictureBackgroundContext() = default;
  Canvas* getCanvas() override {
    return canvas;
  }
  std::shared_ptr<Image> onGetBackgroundImage(Point* offset) override;

 private:
  PictureBackgroundContext(const Matrix& imageMatrix, const Rect& rect,
                           std::shared_ptr<ColorSpace> colorSpace)
      : BackgroundContext(nullptr, imageMatrix, rect, std::move(colorSpace)) {
    canvas = recorder.beginRecording();
  }
  PictureRecorder recorder;
  Canvas* canvas = nullptr;
};

class SurfaceBackgroundContext : public BackgroundContext {
 public:
  static std::shared_ptr<SurfaceBackgroundContext> Make(Context* context, const Matrix& matrix,
                                                        const Rect& rect,
                                                        std::shared_ptr<ColorSpace> colorSpace) {
    auto invertMatrix = Matrix::I();
    matrix.invert(&invertMatrix);
    auto surfaceRect = invertMatrix.mapRect(rect);
    auto surface = Surface::Make(context, static_cast<int>(surfaceRect.width()),
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
  std::shared_ptr<Image> onGetBackgroundImage(Point* offset) override;

 private:
  SurfaceBackgroundContext(std::shared_ptr<Surface> surface, const Matrix& matrix, const Rect& rect,
                           std::shared_ptr<ColorSpace> colorSpace)
      : BackgroundContext(surface->getContext(), matrix, rect, std::move(colorSpace)),
        surface(std::move(surface)) {
  }
  std::shared_ptr<Surface> surface = nullptr;
};

std::shared_ptr<Image> PictureBackgroundContext::onGetBackgroundImage(Point* offset) {
  auto matrix = canvas->getMatrix();
  auto clip = canvas->getTotalClip();
  auto picture = recorder.finishRecordingAsPicture();
  canvas = recorder.beginRecording();
  // save current picture to canvas
  canvas->drawPicture(picture);
  canvas->resetMatrix();
  canvas->clipPath(clip);
  canvas->setMatrix(matrix);
  if (picture == nullptr) {
    return nullptr;
  }
  auto imageBounds = picture->getBounds();
  imageBounds.roundOut();
  auto pictureMatrix = Matrix::MakeTrans(-imageBounds.x(), -imageBounds.y());
  auto image = Image::MakeFrom(std::move(picture), static_cast<int>(imageBounds.width()),
                               static_cast<int>(imageBounds.height()), &pictureMatrix);
  if (offset) {
    *offset = Point::Make(imageBounds.x(), imageBounds.y());
  }
  return image;
}

std::shared_ptr<Image> SurfaceBackgroundContext::onGetBackgroundImage(Point* offset) {
  if (offset) {
    *offset = Point::Make(0, 0);
  }
  auto image = surface->makeImageSnapshot();
  if (!parent) {
    return image;
  }
  PictureRecorder recorder;
  auto canvas = recorder.beginRecording();
  canvas->drawImage(parent->getBackgroundImage(nullptr));
  canvas->drawImage(image);
  auto picture = recorder.finishRecordingAsPicture();
  return Image::MakeFrom(std::move(picture), surface->width(), surface->height(), nullptr,
                         surface->colorSpace());
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
                                                           const Matrix& matrix, std::shared_ptr<ColorSpace> colorSpace) {
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
  std::shared_ptr<BackgroundContext> result = nullptr;
  if (context) {
    result =
        SurfaceBackgroundContext::Make(context, imageMatrix, backgroundRect, colorSpace);
  } else {
    result = PictureBackgroundContext::Make(imageMatrix, backgroundRect, colorSpace);
  }
  if (!result) {
    return result;
  }
  auto canvas = result->getCanvas();
  canvas->clear();
  canvas->setMatrix(surfaceMatrix);
  return result;
}

void BackgroundContext::drawToParent(const Matrix& paintMatrix, const Paint& paint) {
  if (!parent) {
    return;
  }
  auto parentCanvas = parent->getCanvas();
  AutoCanvasRestore autoRestore(parentCanvas);
  auto matrix = parentCanvas->getMatrix();
  auto inverseMatrix = Matrix::I();
  if (!matrix.invert(&inverseMatrix)) {
    return;
  }
  inverseMatrix.postConcat(paintMatrix);
  auto newPaint = paint;
  auto maskFilter = newPaint.getMaskFilter();
  if (maskFilter) {
    newPaint.setMaskFilter(maskFilter->makeWithMatrix(inverseMatrix));
  }
  parentCanvas->resetMatrix();
  auto offset = Point::Make(0, 0);
  auto image = onGetBackgroundImage(&offset);
  parentCanvas->drawImage(image, offset.x, offset.y, &newPaint);
}

std::shared_ptr<Image> BackgroundContext::getBackgroundImage(Point* offset) {
  Point localOffset = {};
  auto image = onGetBackgroundImage(&localOffset);
  if (!parent) {
    if (offset) {
      *offset = localOffset;
    }
    return image;
  }
  PictureRecorder recorder;
  auto canvas = recorder.beginRecording();
  auto parentOffset = Point::Make(0, 0);
  auto parentImage = parent->getBackgroundImage(&parentOffset);
  canvas->drawImage(parentImage, parentOffset.x, parentOffset.y);
  canvas->drawImage(image, localOffset.x, localOffset.y);
  auto picture = recorder.finishRecordingAsPicture();
  auto bounds = picture->getBounds();
  auto imageMatrix = Matrix::MakeTrans(-bounds.x(), -bounds.y());
  if (offset) {
    *offset = Point::Make(bounds.x(), bounds.y());
  }
  return Image::MakeFrom(std::move(picture), static_cast<int>(ceilf(bounds.width())),
                         static_cast<int>(ceilf(bounds.height())), &imageMatrix, colorSpace);
}

std::shared_ptr<BackgroundContext> BackgroundContext::createSubContext() {
  std::shared_ptr<BackgroundContext> child = nullptr;
  if (context) {
    child = SurfaceBackgroundContext::Make(context, imageMatrix, backgroundRect, colorSpace);
  } else {
    child = PictureBackgroundContext::Make(imageMatrix, backgroundRect, colorSpace);
  }
  if (!child) {
    return nullptr;
  }
  auto canvas = getCanvas();
  auto childCanvas = child->getCanvas();
  childCanvas->clear();
  childCanvas->clipPath(canvas->getTotalClip());
  childCanvas->setMatrix(canvas->getMatrix());
  child->parent = this;
  return child;
}

}  // namespace tgfx
