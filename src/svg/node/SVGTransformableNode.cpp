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

#include "tgfx/svg/node/SVGTransformableNode.h"
#include "svg/SVGRenderContext.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Rect.h"
#include "tgfx/svg/SVGAttribute.h"
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/SVGValue.h"

namespace tgfx {

SVGTransformableNode::SVGTransformableNode(SVGTag tag) : INHERITED(tag), fTransform(Matrix::I()) {
}

bool SVGTransformableNode::onPrepareToRender(SVGRenderContext* context) const {
  if (!fTransform.isIdentity()) {
    auto transform = fTransform;
    if (auto unit = context->lengthContext().getPatternUnits();
        unit.has_value() &&
        unit.value().type() == SVGObjectBoundingBoxUnits::Type::ObjectBoundingBox) {
      transform.postScale(context->lengthContext().viewPort().width,
                          context->lengthContext().viewPort().height);
    }
    context->saveOnce();
    context->canvas()->concat(transform);
    context->concat(transform);
  }

  return this->INHERITED::onPrepareToRender(context);
}

void SVGTransformableNode::onSetAttribute(SVGAttribute attr, const SVGValue& v) {
  switch (attr) {
    case SVGAttribute::Transform:
      if (const auto* transform = v.as<SVGTransformValue>()) {
        this->setTransform(*transform);
      }
      break;
    default:
      this->INHERITED::onSetAttribute(attr, v);
      break;
  }
}

void SVGTransformableNode::mapToParent(Path* path) const {
  // transforms the path to parent node coordinates.
  path->transform(fTransform);
}

void SVGTransformableNode::mapToParent(Rect* rect) const {
  *rect = fTransform.mapRect(*rect);
}

}  // namespace tgfx