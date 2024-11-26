/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making libpag available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "SolidContent.h"
#include "core/utils/Profiling.h"

namespace tgfx {
SolidContent::SolidContent(const RRect& rRect, const Color& color) : _rRect(rRect), _color(color) {
}

void SolidContent::draw(Canvas* canvas, const Paint& paint) const {
  TRACE_EVENT;
  auto solidPaint = paint;
  auto color = _color;
  color.alpha *= paint.getAlpha();
  solidPaint.setColor(color);
  canvas->drawRRect(_rRect, solidPaint);
}

bool SolidContent::hitTestPoint(float localX, float localY, bool /*pixelHitTest*/) {
  TRACE_EVENT;
  return _rRect.rect.contains(localX, localY);
}

}  // namespace tgfx