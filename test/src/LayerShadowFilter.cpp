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
#include <utility>
#include "tgfx/core/Canvas.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Recorder.h"

namespace tgfx {

std::shared_ptr<LayerShadowFilter> LayerShadowFilter::Make(
    const std::vector<LayerShadowParam>& params) {
  return std::shared_ptr<LayerShadowFilter>(new LayerShadowFilter(params));
}

LayerShadowFilter::LayerShadowFilter(const std::vector<LayerShadowParam>& params) : params(params) {
}

void LayerShadowFilter::setShadowParams(const std::vector<LayerShadowParam>& shadowParams) {
  if (shadowParams == params) {
    return;
  }
  params = shadowParams;
  invalidate();
}

void LayerShadowFilter::setShowBehindTransparent(bool showBehindTransparent) {
  if (showBehindTransparent == _showBehindTransparent) {
    return;
  }
  _showBehindTransparent = showBehindTransparent;
  invalidate();
}

bool LayerShadowFilter::applyFilter(Canvas* canvas, std::shared_ptr<Image> image,
                                    float contentScale) {
  if (!image) {
    return false;
  }

  drawShadows(canvas, image, contentScale);

  canvas->drawImage(image);
  return true;
}

Rect LayerShadowFilter::filterBounds(const Rect& srcRect, float scale) {
  auto maxRect = srcRect;
  for (const auto& param : params) {
    auto filter = CreateShadowFilter(param, scale);
    if (!filter) {
      continue;
    }
    maxRect.join(filter->filterBounds(srcRect));
  }
  return maxRect;
}

std::shared_ptr<ImageFilter> LayerShadowFilter::CreateShadowFilter(const LayerShadowParam& param,
                                                                   float scale) {
  return ImageFilter::DropShadowOnly(param.offsetX * scale, param.offsetY * scale,
                                     param.blurrinessX * scale, param.blurrinessY * scale,
                                     param.color);
}

void LayerShadowFilter::drawShadows(Canvas* canvas, std::shared_ptr<Image> image,
                                    float contentScale) {
  // create opaque image
  auto opaqueFilter = ImageFilter::ColorFilter(ColorFilter::AlphaThreshold(0));
  auto opaqueImage = image->makeWithFilter(opaqueFilter);

  auto shader = Shader::MakeImageShader(opaqueImage, TileMode::Decal, TileMode::Decal);
  Point offset = Point::Zero();

  for (const auto& param : params) {
    if (auto filter = CreateShadowFilter(param, contentScale)) {
      auto shadowImage = opaqueImage->makeWithFilter(filter, &offset);
      Paint paint{};
      if (!_showBehindTransparent) {
        auto matrixShader = shader->makeWithMatrix(Matrix::MakeTrans(-offset.x, -offset.y));
        paint.setMaskFilter(MaskFilter::MakeShader(matrixShader, true));
      }
      canvas->drawImage(shadowImage, offset.x, offset.y, &paint);
    }
  }
}

}  // namespace tgfx