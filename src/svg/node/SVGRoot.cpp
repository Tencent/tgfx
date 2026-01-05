/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "tgfx/svg/node/SVGRoot.h"
#include "svg/SVGRenderContext.h"
#include "tgfx/core/Size.h"
#include "tgfx/svg/SVGAttribute.h"
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/SVGValue.h"

namespace tgfx {

void SVGRoot::renderNode(const SVGRenderContext& context, const SVGIRI& iri) const {
  SVGRenderContext localContext(context, this);
  auto node = localContext.findNodeById(iri);
  if (!node) {
    return;
  }

  if (this->onPrepareToRender(&localContext)) {
    if (this == node.get()) {
      this->onRender(context);
    } else {
      node->render(localContext);
    }
  }
}

bool SVGRoot::onPrepareToRender(SVGRenderContext* context) const {
  // x/y are ignored for outermost svg elements
  const auto x = type == Type::kInner ? X : SVGLength(0);
  const auto y = type == Type::kInner ? Y : SVGLength(0);

  auto viewPortRect = context->lengthContext().resolveRect(x, y, Width, Height);
  auto contentMatrix = Matrix::MakeTrans(viewPortRect.x(), viewPortRect.y());
  auto viewPort = Size::Make(viewPortRect.width(), viewPortRect.height());

  if (ViewBox.has_value()) {
    const Rect& viewBox = *ViewBox;

    // An empty viewbox disables rendering.
    if (viewBox.isEmpty()) {
      return false;
    }

    // A viewBox overrides the intrinsic viewport.
    viewPort = Size::Make(viewBox.width(), viewBox.height());

    contentMatrix.preConcat(ComputeViewboxMatrix(viewBox, viewPortRect, PreserveAspectRatio));
  }

  if (!contentMatrix.isIdentity()) {
    context->saveOnce();
    context->canvas()->concat(contentMatrix);
  }

  if (viewPort != context->lengthContext().viewPort()) {
    context->writableLengthContext()->setViewPort(viewPort);
  }

  return this->INHERITED::onPrepareToRender(context);
}

// https://www.w3.org/TR/SVG11/coords.html#IntrinsicSizing
Size SVGRoot::intrinsicSize(const SVGLengthContext& lengthContext) const {
  // Percentage values do not provide an intrinsic size.
  if (Width.unit() == SVGLength::Unit::Percentage || Height.unit() == SVGLength::Unit::Percentage) {
    return Size::Make(0, 0);
  }

  return Size::Make(lengthContext.resolve(Width, SVGLengthContext::LengthType::Horizontal),
                    lengthContext.resolve(Height, SVGLengthContext::LengthType::Vertical));
}

void SVGRoot::onSetAttribute(SVGAttribute attr, const SVGValue& v) {
  if (type != Type::kInner && type != Type::kRoot) return;
  switch (attr) {
    case SVGAttribute::X:
      if (const auto x = v.as<SVGLengthValue>()) {
        SVGLength xValue = *x;
        this->setX(xValue);
      }
      break;
    case SVGAttribute::Y:
      if (const auto y = v.as<SVGLengthValue>()) {
        SVGLength yValue = *y;
        this->setY(yValue);
      }
      break;
    case SVGAttribute::Width:
      if (const auto w = v.as<SVGLengthValue>()) {
        SVGLength wValue = *w;
        this->setWidth(wValue);
      }
      break;
    case SVGAttribute::Height:
      if (const auto h = v.as<SVGLengthValue>()) {
        SVGLength hValue = *h;
        this->setHeight(hValue);
      }
      break;
    case SVGAttribute::ViewBox:
      if (const auto vb = v.as<SVGViewBoxValue>()) {
        SVGViewBoxType vbValue = *vb;
        this->setViewBox(vbValue);
      }
      break;
    case SVGAttribute::PreserveAspectRatio:
      if (const auto par = v.as<SVGPreserveAspectRatioValue>()) {
        SVGPreserveAspectRatio parValue = *par;
        this->setPreserveAspectRatio(parValue);
      }
      break;
    default:
      this->INHERITED::onSetAttribute(attr, v);
  }
}
}  // namespace tgfx
