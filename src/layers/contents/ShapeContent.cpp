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

#include "ShapeContent.h"

namespace tgfx {
ShapeContent::ShapeContent(std::shared_ptr<Shape> shape, std::shared_ptr<Shader> shader)
    : shape(std::move(shape)), shader(std::move(shader)) {
}

void ShapeContent::draw(Canvas* canvas, const Paint& paint) const {
  TGFX_PROFILE_ZONE_SCOPPE_NAME("ShapeContent::draw");
  auto shapePaint = paint;
  shapePaint.setShader(shader);
  canvas->drawShape(shape, shapePaint);
}

bool ShapeContent::hitTestPoint(float localX, float localY, bool pixelHitTest) {
  TGFX_PROFILE_ZONE_SCOPPE_NAME("ShapeContent::draw");
  if (pixelHitTest) {
    auto path = shape->getPath();
    return path.contains(localX, localY);
  }
  const auto bounds = shape->getBounds();
  return bounds.contains(localX, localY);
}
}  // namespace tgfx
