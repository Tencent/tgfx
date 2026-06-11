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

#include "layers/SpreadUtils.h"
#include <algorithm>
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "layers/LayerStyleSource.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/PictureRecorder.h"
#include "tgfx/core/RRect.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/Stroke.h"

namespace tgfx {

static inline RRect MakeSpreadRRect(const RRect& rRect, float distance) {
  auto bounds = rRect.rect();
  bounds.outset(distance, distance);
  if (bounds.width() <= 0.0f || bounds.height() <= 0.0f) {
    return {};
  }
  // Adjust radii by the same amount so the expanded/contracted corners stay concentric with the original.
  auto radii = rRect.radii();
  for (auto& corner : radii) {
    corner.x = std::max(0.0f, corner.x + distance);
    corner.y = std::max(0.0f, corner.y + distance);
  }
  RRect result = {};
  result.setRectRadii(bounds, radii);
  return result;
}

static inline void DrawSpreadRRect(Canvas* canvas, const RRect& rRect, PaintStyle style,
                                   float strokeWidth, float fillOutset, float spread) {
  Paint paint = {};
  paint.setColor(Color::White());
  paint.setAntiAlias(true);
  if (style == PaintStyle::Fill) {
    auto outset = fillOutset + spread;
    if (outset != 0) {
      auto spreadRRect = MakeSpreadRRect(rRect, outset);
      if (spreadRRect.rect().isEmpty()) {
        return;
      }
      canvas->drawRRect(spreadRRect, paint);
    } else {
      canvas->drawRRect(rRect, paint);
    }
  } else {
    DEBUG_ASSERT(strokeWidth * 0.5f + spread > 0.0f);
    paint.setStyle(PaintStyle::Stroke);
    paint.setStroke(Stroke(strokeWidth + 2.0f * spread));
    canvas->drawRRect(rRect, paint);
  }
}

SpreadUtils::OffsetImage SpreadUtils::MakeSpreadShapeImage(const LayerStyleInput& input,
                                                           float spread) {
  if (!input.contentShape.has_value()) {
    return {nullptr, {}};
  }
  auto& styledShape = *input.contentShape;
  DEBUG_ASSERT(styledShape.shape != nullptr);
  if (styledShape.shape == nullptr || styledShape.shape->getPath().isEmpty()) {
    return {nullptr, {}};
  }
  // Stroke fully collapsed by negative spread.
  if (styledShape.style == PaintStyle::Stroke && styledShape.strokeWidth * 0.5f + spread <= 0.0f) {
    return {nullptr, {}};
  }

  auto path = styledShape.shape->getPath();
  PictureRecorder recorder;
  auto* recordCanvas = recorder.beginRecording();
  recordCanvas->scale(input.contentScale, input.contentScale);

  Rect rect = {};
  RRect rRect = {};
  auto style = styledShape.style;
  auto strokeWidth = styledShape.strokeWidth;
  auto fillOutset = styledShape.fillOutset;
  if (path.isOval(&rect)) {
    DrawSpreadRRect(recordCanvas, RRect::MakeOval(rect), style, strokeWidth, fillOutset, spread);
  } else if (path.isRRect(&rRect)) {
    DrawSpreadRRect(recordCanvas, rRect, style, strokeWidth, fillOutset, spread);
  } else {
    if (!path.isRect(&rect)) {
      // Complex paths use their bounding rect as a fill approximation for the shadow source.
      rect = path.getBounds();
      style = PaintStyle::Fill;
    }
    DrawSpreadRRect(recordCanvas, RRect::MakeRectXY(rect, 0, 0), style, strokeWidth, fillOutset,
                    spread);
  }

  auto picture = recorder.finishRecordingAsPicture();
  Point offset = {};
  auto image = ToImageWithOffset(std::move(picture), &offset);
  if (image == nullptr) {
    DEBUG_ASSERT(false);
    return {nullptr, {}};
  }
  return {std::move(image),
          {offset.x - input.contentOffset.x * input.contentScale,
           offset.y - input.contentOffset.y * input.contentScale}};
}

}  // namespace tgfx
