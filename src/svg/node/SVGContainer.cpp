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

#include "tgfx/svg/node/SVGContainer.h"
#include "../../../include/tgfx/core/Log.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/PathTypes.h"
#include "tgfx/core/Rect.h"
#include "tgfx/svg/node/SVGNode.h"

namespace tgfx {

class SVGRenderContext;

SVGContainer::SVGContainer(SVGTag t) : INHERITED(t) {
}

void SVGContainer::appendChild(std::shared_ptr<SVGNode> node) {
  ASSERT(node);
  children.push_back(std::move(node));
}

const std::vector<std::shared_ptr<SVGNode>>& SVGContainer::getChildren() const {
  return children;
}

bool SVGContainer::hasChildren() const {
  return !children.empty();
}

void SVGContainer::onRender(const SVGRenderContext& context) const {
  for (const auto& i : children) {
    i->render(context);
  }
}

Path SVGContainer::onAsPath(const SVGRenderContext& context) const {
  Path path;

  for (const auto& child : children) {
    const Path childPath = child->asPath(context);
    path.addPath(childPath, PathOp::Union);
  }

  this->mapToParent(&path);
  return path;
}

Rect SVGContainer::onObjectBoundingBox(const SVGRenderContext& context) const {
  Rect bounds = {};
  for (const auto& child : children) {
    const Rect childBounds = child->objectBoundingBox(context);
    bounds.join(childBounds);
  }

  return bounds;
}

}  // namespace tgfx