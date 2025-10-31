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

#include "BlendModeContext.h"

namespace tgfx {

BlendModeContext::BlendModeContext(float scale) {
  canvas = recorder.beginRecording();
  canvas->scale(scale, scale);
}

std::shared_ptr<Image> BlendModeContext::getImage(Matrix* imageMatrix) {
  auto matrix = canvas->getMatrix();
  auto clip = canvas->getTotalClip();
  auto picture = recorder.finishRecordingAsPicture();
  canvas = recorder.beginRecording();
  if (picture == nullptr) {
    return nullptr;
  }
  // save current picture to canvas
  canvas->drawPicture(picture);
  canvas->resetMatrix();
  canvas->clipPath(clip);
  canvas->setMatrix(matrix);
  auto imageBounds = picture->getBounds();
  imageBounds.roundOut();
  auto pictureMatrix = Matrix::MakeTrans(-imageBounds.x(), -imageBounds.y());
  auto image = Image::MakeFrom(std::move(picture), static_cast<int>(imageBounds.width()),
                               static_cast<int>(imageBounds.height()), &pictureMatrix);
  if (imageMatrix) {
    matrix.invert(imageMatrix);
    imageMatrix->preTranslate(imageBounds.x(), imageBounds.y());
  }
  return image;
}

Canvas* BlendModeContext::getCanvas() {
  return canvas;
}

}  // namespace tgfx
