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
#include "tgfx/core/Recorder.h"

namespace tgfx {

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
                                                           const Matrix& matrix) {
  if (context == nullptr) {
    return nullptr;
  }
  auto backgroundContext = std::shared_ptr<BackgroundContext>(new BackgroundContext());
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

  if (!surfaceMatrix.invert(&backgroundContext->imageMatrix)) {
    return nullptr;
  }
  backgroundContext->surface =
      Surface::Make(context, static_cast<int>(rect.width()), static_cast<int>(rect.height()));
  if (!backgroundContext->surface) {
    return nullptr;
  }
  auto backgroundRect =
      Rect::MakeWH(backgroundContext->surface->width(), backgroundContext->surface->height());
  backgroundContext->imageMatrix.mapRect(&backgroundRect);
  backgroundContext->backgroundRect = backgroundRect;
  auto canvas = backgroundContext->getCanvas();
  canvas->clear();
  canvas->setMatrix(surfaceMatrix);
  return backgroundContext;
}

std::shared_ptr<Image> BackgroundContext::getBackgroundImage() const {
  auto image = surface->makeImageSnapshot();
  if (!parent) {
    return image;
  }
  Recorder recorder;
  auto canvas = recorder.beginRecording();
  canvas->drawImage(parent->getBackgroundImage());
  canvas->drawImage(image);
  auto picture = recorder.finishRecordingAsPicture();
  return Image::MakeFrom(std::move(picture), surface->width(), surface->height(), nullptr,
                         surface->colorSpace());
}

std::shared_ptr<BackgroundContext> BackgroundContext::createSubContext() const {
  if (!surface) {
    return nullptr;
  }

  auto child = std::shared_ptr<BackgroundContext>(new BackgroundContext());
  child->surface = Surface::Make(surface->getContext(), surface->width(), surface->height());
  if (!child->surface) {
    return nullptr;
  }
  auto canvas = getCanvas();
  auto childCanvas = child->getCanvas();
  childCanvas->clear();
  childCanvas->clipPath(canvas->getTotalClip());
  childCanvas->setMatrix(canvas->getMatrix());
  child->parent = this;
  child->imageMatrix = imageMatrix;
  child->backgroundRect = backgroundRect;
  return child;
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
  auto shader = newPaint.getShader();
  if (shader) {
    newPaint.setShader(shader->makeWithMatrix(inverseMatrix));
  }
  parentCanvas->resetMatrix();
  parentCanvas->drawImage(surface->makeImageSnapshot(), &newPaint);
}

}  // namespace tgfx
