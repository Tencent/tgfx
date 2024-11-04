/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "tgfx/core/Surface.h"

namespace drawers {
void ImageWithShadow::onDraw(tgfx::Canvas* canvas, const drawers::AppHost* host) {
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
  tgfx::SamplingOptions sampling(tgfx::FilterMode::Linear, tgfx::MipmapMode::Linear);
  auto surface = tgfx::Surface::Make(canvas->getSurface()->getContext(), size, size);
  auto scaledCanvas = surface->getCanvas();
  tgfx::Path path = {};
  path.addOval(tgfx::Rect::MakeXYWH(0, 0, size, size));
  scaledCanvas->clipPath(path);
  scaledCanvas->setMatrix(matrix);
  scaledCanvas->drawImage(image, sampling);
  auto scaledImage = surface->makeImageSnapshot();

  auto filter = tgfx::ImageFilter::DropShadow(5 * scale, 5 * scale, 50 * scale, 50 * scale,
                                              tgfx::Color::Black());
  tgfx::Paint paint = {};
  paint.setImageFilter(filter);
  canvas->drawImage(scaledImage, static_cast<float>(width - size) / 2,
                    static_cast<float>(height - size) / 2, &paint);
}
}  // namespace drawers
