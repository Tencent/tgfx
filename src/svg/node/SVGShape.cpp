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

#include "tgfx/svg/node/SVGShape.h"
#include <memory>
#include "core/utils/Log.h"
#include "svg/SVGRenderContext.h"
#include "tgfx/core/Size.h"

namespace tgfx {

SVGShape::SVGShape(SVGTag t) : INHERITED(t) {
}

void SVGShape::onRender(const SVGRenderContext& context) const {
  const auto fillType = context.presentationContext()._inherited.FillRule->asFillType();

  auto selfRect = onObjectBoundingBox(context);
  auto lengthCtx = context.lengthContext();
  lengthCtx.setViewPort(Size::Make(selfRect.width(), selfRect.height()));
  auto paintCtx = SVGRenderContext::CopyForPaint(context, context.canvas(), lengthCtx);

  const auto fillPaint = paintCtx.fillPaint();
  const auto strokePaint = paintCtx.strokePaint();

  if (fillPaint.has_value()) {
    this->onDraw(context.canvas(), context.lengthContext(), fillPaint.value(), fillType);
  }

  if (strokePaint.has_value()) {
    this->onDraw(context.canvas(), context.lengthContext(), strokePaint.value(), fillType);
  }
}

void SVGShape::appendChild(std::shared_ptr<SVGNode>) {
  LOGE("cannot append child nodes to an SVG shape.\n");
}
}  // namespace tgfx