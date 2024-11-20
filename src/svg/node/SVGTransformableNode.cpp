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
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Rect.h"
#include "tgfx/svg/SVGAttribute.h"
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/SVGValue.h"

namespace tgfx {

SkSVGTransformableNode::SkSVGTransformableNode(SVGTag tag)
    : INHERITED(tag), fTransform(Matrix::I()) {
}

#ifndef RENDER_SVG
bool SkSVGTransformableNode::onPrepareToRender(SVGRenderContext* ctx) const {
  if (!fTransform.isIdentity()) {
    auto transform = fTransform;
    if (auto unit = ctx->lengthContext().getPatternUnits();
        unit.has_value() &&
        unit.value().type() == SVGObjectBoundingBoxUnits::Type::kObjectBoundingBox) {
      transform.postScale(ctx->lengthContext().viewPort().width,
                          ctx->lengthContext().viewPort().height);
    }
    ctx->saveOnce();
    ctx->canvas()->concat(transform);
  }

  return this->INHERITED::onPrepareToRender(ctx);
}
#endif

void SkSVGTransformableNode::onSetAttribute(SVGAttribute attr, const SVGValue& v) {
  switch (attr) {
    case SVGAttribute::kTransform:
      if (const auto* transform = v.as<SVGTransformValue>()) {
        this->setTransform(*transform);
      }
      break;
    default:
      this->INHERITED::onSetAttribute(attr, v);
      break;
  }
}

#ifndef RENDER_SVG
void SkSVGTransformableNode::mapToParent(Path* path) const {
  // transforms the path to parent node coordinates.
  path->transform(fTransform);
}

void SkSVGTransformableNode::mapToParent(Rect* rect) const {
  *rect = fTransform.mapRect(*rect);
}
#endif
}  // namespace tgfx