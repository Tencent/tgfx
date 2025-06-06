/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "tgfx/core/Recorder.h"

namespace tgfx {
std::shared_ptr<BackgroundContext> BackgroundContext::Make(Context* context, const Rect& drawRect,
                                                           const Matrix& matrix) {
  if (context == nullptr) {
    return nullptr;
  }
  auto backgroundContext = std::shared_ptr<BackgroundContext>(new BackgroundContext());
  auto rect = drawRect;
  rect.roundOut();
  auto surfaceMatrix = Matrix::MakeTrans(-rect.x(), -rect.y());
  surfaceMatrix.preConcat(matrix);
  if (!surfaceMatrix.invert(&backgroundContext->imageMatrix)) {
    return nullptr;
  }
  backgroundContext->surface =
      Surface::Make(context, static_cast<int>(rect.width()), static_cast<int>(rect.height()));
  if (!backgroundContext->surface) {
    return nullptr;
  }
  auto canvas = backgroundContext->backgroundCanvas();
  canvas->clear();
  canvas->setMatrix(surfaceMatrix);
  return backgroundContext;
}

Canvas* BackgroundContext::backgroundCanvas() const {
  return surface->getCanvas();
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
  return Image::MakeFrom(std::move(picture), surface->width(), surface->height());
}

std::shared_ptr<BackgroundContext> BackgroundContext::creatSubContext() const {
  if (!surface) {
    return nullptr;
  }
  auto child =
      Make(surface->getContext(), Rect::MakeWH(surface->width(), surface->height()), Matrix::I());
  if (!child) {
    return nullptr;
  }
  auto childCanvas = child->backgroundCanvas();
  childCanvas->clipPath(backgroundCanvas()->getTotalClip());
  childCanvas->setMatrix(backgroundCanvas()->getMatrix());
  child->parent = this;
  child->imageMatrix = imageMatrix;
  return child;
}

void BackgroundContext::drawToParent(const Matrix& paintMatrix, const Paint& paint) {
  if (!parent) {
    return;
  }
  auto parentCanvas = parent->backgroundCanvas();
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
