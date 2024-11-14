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

#include "tgfx/svg/node/SVGSVG.h"
#include "tgfx/core/Size.h"
#include "tgfx/svg/SVGAttribute.h"
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/SVGValue.h"

namespace tgfx {

#ifndef RENDER_SVG
void SVGSVG::renderNode(const SVGRenderContext& ctx, const SVGIRI& iri) const {
  SVGRenderContext localContext(ctx, this);
  auto node = localContext.findNodeById(iri);
  if (!node) {
    return;
  }

  if (this->onPrepareToRender(&localContext)) {
    if (this == node.get()) {
      this->onRender(ctx);
    } else {
      node->render(localContext);
    }
  }
}

bool SVGSVG::onPrepareToRender(SVGRenderContext* ctx) const {
  // x/y are ignored for outermost svg elements
  const auto x = fType == Type::kInner ? fX : SVGLength(0);
  const auto y = fType == Type::kInner ? fY : SVGLength(0);

  auto viewPortRect = ctx->lengthContext().resolveRect(x, y, fWidth, fHeight);
  auto contentMatrix = Matrix::MakeTrans(viewPortRect.x(), viewPortRect.y());
  auto viewPort = Size::Make(viewPortRect.width(), viewPortRect.height());

  if (fViewBox.has_value()) {
    const Rect& viewBox = *fViewBox;

    // An empty viewbox disables rendering.
    if (viewBox.isEmpty()) {
      return false;
    }

    // A viewBox overrides the intrinsic viewport.
    viewPort = Size::Make(viewBox.width(), viewBox.height());

    contentMatrix.preConcat(ComputeViewboxMatrix(viewBox, viewPortRect, fPreserveAspectRatio));
  }

  if (!contentMatrix.isIdentity()) {
    ctx->saveOnce();
    ctx->canvas()->concat(contentMatrix);
  }

  if (viewPort != ctx->lengthContext().viewPort()) {
    ctx->writableLengthContext()->setViewPort(viewPort);
  }

  return this->INHERITED::onPrepareToRender(ctx);
}

// https://www.w3.org/TR/SVG11/coords.html#IntrinsicSizing
Size SVGSVG::intrinsicSize(const SVGLengthContext& lctx) const {
  // Percentage values do not provide an intrinsic size.
  if (fWidth.unit() == SVGLength::Unit::kPercentage ||
      fHeight.unit() == SVGLength::Unit::kPercentage) {
    return Size::Make(0, 0);
  }

  return Size::Make(lctx.resolve(fWidth, SVGLengthContext::LengthType::kHorizontal),
                    lctx.resolve(fHeight, SVGLengthContext::LengthType::kVertical));
}
#endif

void SVGSVG::onSetAttribute(SVGAttribute attr, const SVGValue& v) {
  if (fType != Type::kInner && fType != Type::kRoot) return;
  switch (attr) {
    case SVGAttribute::kX:
      if (const auto* x = v.as<SVGLengthValue>()) {
        SVGLength xValue = *x;
        this->setX(xValue);
      }
      break;
    case SVGAttribute::kY:
      if (const auto* y = v.as<SVGLengthValue>()) {
        SVGLength yValue = *y;
        this->setY(yValue);
      }
      break;
    case SVGAttribute::kWidth:
      if (const auto* w = v.as<SVGLengthValue>()) {
        SVGLength wValue = *w;
        this->setWidth(wValue);
      }
      break;
    case SVGAttribute::kHeight:
      if (const auto* h = v.as<SVGLengthValue>()) {
        SVGLength hValue = *h;
        this->setHeight(hValue);
      }
      break;
    case SVGAttribute::kViewBox:
      if (const auto* vb = v.as<SVGViewBoxValue>()) {
        SVGViewBoxType vbValue = *vb;
        this->setViewBox(vbValue);
      }
      break;
    case SVGAttribute::kPreserveAspectRatio:
      if (const auto* par = v.as<SVGPreserveAspectRatioValue>()) {
        SVGPreserveAspectRatio parValue = *par;
        this->setPreserveAspectRatio(parValue);
      }
      break;
    default:
      this->INHERITED::onSetAttribute(attr, v);
  }
}
}  // namespace tgfx