/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "LayerShadowFilter.h"
#include <tgfx/core/Recorder.h>
#include "tgfx/core/Canvas.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Paint.h"

namespace tgfx {

std::shared_ptr<LayerShadowFilter> LayerShadowFilter::Make(
    const std::vector<LayerShadowParam>& params) {
  return std::shared_ptr<LayerShadowFilter>(new LayerShadowFilter(params));
}

LayerShadowFilter::LayerShadowFilter(const std::vector<LayerShadowParam>& params) : params(params) {
}

void LayerShadowFilter::setShadowParams(const std::vector<LayerShadowParam>& shadowParams) {
  params = shadowParams;
  invalidate();
}

bool LayerShadowFilter::drawWithFilter(Canvas* canvas, std::shared_ptr<Picture> picture,
                                       float scale) {
  auto bounds = picture->getBounds();
  bounds.roundOut();
  Matrix matrix = Matrix::MakeTrans(-bounds.x(), -bounds.y());
  auto image = Image::MakeFrom(picture, static_cast<int>(bounds.width()),
                               static_cast<int>(bounds.height()), &matrix);
  if (!image) {
    return false;
  }

  // record shadow picture
  Recorder shadowRecorder;
  auto shadowCanvas = shadowRecorder.beginRecording();
  for (const auto& param : params) {
    auto filter = createShadowFilter(param, scale);
    if (!filter) {
      continue;
    }
    Paint paint;
    paint.setImageFilter(filter);
    shadowCanvas->drawImage(image, bounds.x(), bounds.y(), &paint);
  }
  auto shadowPicture = shadowRecorder.finishRecordingAsPicture();

  // Draw shadow beside the original picture area
  auto boundsAfterFilter = filterBounds(bounds, scale);
  auto shader = Shader::MakeImageShader(image, TileMode::Decal, TileMode::Decal);
  shader = shader->makeWithMatrix(Matrix::MakeTrans(-boundsAfterFilter.left, -boundsAfterFilter.top));
  auto maskFilter = MaskFilter::MakeShader(shader, true);
  Paint shadowPaint;
  shadowPaint.setMaskFilter(maskFilter);
  canvas->drawPicture(shadowPicture, nullptr, &shadowPaint);

  // draw original image
  canvas->drawImage(image, bounds.x(), bounds.y());
  return true;
}

Rect LayerShadowFilter::filterBounds(const Rect& srcRect, float scale) {
  auto maxRect = srcRect;
  for (const auto& param : params) {
    auto filter = createShadowFilter(param, scale);
    if (!filter) {
      continue;
    }
    maxRect.join(filter->filterBounds(srcRect));
  }
  return maxRect;
}

std::shared_ptr<ImageFilter> LayerShadowFilter::createShadowFilter(const LayerShadowParam& param,
                                                                   float scale) {
  return ImageFilter::DropShadowOnly(param.offsetX * scale, param.offsetY * scale,
                                     param.blurrinessX * scale, param.blurrinessY * scale,
                                     param.color);
}

}  // namespace tgfx