/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "layers/CanvasUtils.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Surface.h"

namespace tgfx {

std::optional<Rect> GetClipBounds(const Canvas* canvas) {
  if (canvas == nullptr) {
    return std::nullopt;
  }
  const auto clipBounds = canvas->getTotalClipBounds();
  auto clipRect = Rect::MakeEmpty();
  auto surface = canvas->getSurface();
  if (!clipBounds.has_value()) {
    if (!surface) {
      return std::nullopt;
    }
    clipRect = Rect::MakeWH(surface->width(), surface->height());
  } else {
    clipRect = *clipBounds;
    if (surface && !clipRect.intersect(Rect::MakeWH(surface->width(), surface->height()))) {
      return Rect::MakeEmpty();
    }
  }
  if (clipRect.isEmpty()) {
    return Rect::MakeEmpty();
  }
  auto inverse = Matrix::I();
  if (!canvas->getMatrix().invert(&inverse)) {
    return Rect::MakeEmpty();
  }
  clipRect = inverse.mapRect(clipRect);
  clipRect.roundOut();
  return clipRect;
}

}  // namespace tgfx
