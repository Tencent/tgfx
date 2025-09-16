/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "base/Drawers.h"

namespace drawers {
void ImageWithMipmap::onDraw(tgfx::Canvas* canvas, const drawers::AppHost* host) {
  auto scale = host->density();
  auto width = host->width();
  auto height = host->height();
  auto screenSize = std::min(width, height);
  auto size = screenSize - static_cast<int>(150 * scale);
  size = std::max(size, 50);
  auto image = host->getImage("bridge");
  if (image == nullptr) {
    return;
  }
  image = image->makeMipmapped(true);
  auto imageScale = static_cast<float>(size) / static_cast<float>(image->width());
  auto matrix = tgfx::Matrix::MakeScale(imageScale);
  matrix.postTranslate(static_cast<float>(width - size) / 2, static_cast<float>(height - size) / 2);
  matrix.postScale(host->zoomScale(), host->zoomScale());
  matrix.postTranslate(host->contentOffset().x, host->contentOffset().y);
  canvas->concat(matrix);
  tgfx::SamplingOptions sampling(tgfx::FilterMode::Linear, tgfx::MipmapMode::Linear);
  canvas->drawImage(image, sampling);
}
}  // namespace drawers
