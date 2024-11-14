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

#include "tgfx/svg/node/SVGContainer.h"
#include "core/utils/Log.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/PathTypes.h"
#include "tgfx/core/Rect.h"
#include "tgfx/svg/node/SVGNode.h"

namespace tgfx {

class SVGRenderContext;

SkSVGContainer::SkSVGContainer(SVGTag t) : INHERITED(t) {
}

void SkSVGContainer::appendChild(std::shared_ptr<SVGNode> node) {
  ASSERT(node);
  fChildren.push_back(std::move(node));
}

const std::vector<std::shared_ptr<SVGNode>>& SkSVGContainer::getChildren() const {
  return fChildren;
}

bool SkSVGContainer::hasChildren() const {
  return !fChildren.empty();
}

#ifndef RENDER_SVG
void SkSVGContainer::onRender(const SVGRenderContext& ctx) const {
  for (const auto& i : fChildren) {
    i->render(ctx);
  }
}

Path SkSVGContainer::onAsPath(const SVGRenderContext& ctx) const {
  Path path;

  for (const auto& i : fChildren) {
    const Path childPath = i->asPath(ctx);
    path.addPath(childPath, PathOp::Union);
  }

  this->mapToParent(&path);
  return path;
}

Rect SkSVGContainer::onObjectBoundingBox(const SVGRenderContext& ctx) const {
  Rect bounds = Rect::MakeEmpty();

  for (const auto& i : fChildren) {
    const Rect childBounds = i->objectBoundingBox(ctx);
    bounds.join(childBounds);
  }

  return bounds;
}
#endif
}  // namespace tgfx