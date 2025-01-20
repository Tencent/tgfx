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

#pragma once

#include <memory>
#include "tgfx/core/Path.h"
#include "tgfx/core/Rect.h"
#include "tgfx/svg/node/SVGNode.h"
#include "tgfx/svg/node/SVGTransformableNode.h"

namespace tgfx {

class SVGRenderContext;

class SVGContainer : public SVGTransformableNode {
 public:
  void appendChild(std::shared_ptr<SVGNode> node) override;
  const std::vector<std::shared_ptr<SVGNode>>& getChildren() const;
  bool hasChildren() const final;

 protected:
  explicit SVGContainer(SVGTag);

  void onRender(const SVGRenderContext& context) const override;

  Path onAsPath(const SVGRenderContext& context) const override;

  Rect onObjectBoundingBox(const SVGRenderContext& context) const override;

  template <typename NodeType, typename Func>
  void forEachChild(Func func) const {
    for (const auto& child : children) {
      if (child->tag() == NodeType::tag) {
        func(static_cast<const NodeType*>(child.get()));
      }
    }
  }

  std::vector<std::shared_ptr<SVGNode>> children;

 private:
  using INHERITED = SVGTransformableNode;
};

}  // namespace tgfx
