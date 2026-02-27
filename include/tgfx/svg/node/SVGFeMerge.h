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

#pragma once

#include <memory>
#include <vector>
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/node/SVGFe.h"
#include "tgfx/svg/node/SVGHiddenContainer.h"
#include "tgfx/svg/node/SVGNode.h"

namespace tgfx {

// https://www.w3.org/TR/SVG11/filters.html#feMergeNodeElement
class SVGFeMergeNode : public SVGHiddenContainer {
 public:
  static constexpr SVGTag tag = SVGTag::FeMergeNode;

  static std::shared_ptr<SVGFeMergeNode> Make() {
    return std::shared_ptr<SVGFeMergeNode>(new SVGFeMergeNode());
  }

  SVG_ATTR(In, SVGFeInputType, SVGFeInputType())

 protected:
  bool parseAndSetAttribute(const std::string& name, const std::string& value) override;

 private:
  SVGFeMergeNode() : INHERITED(tag) {
  }

  using INHERITED = SVGHiddenContainer;
};

// https://www.w3.org/TR/SVG11/filters.html#feMergeElement
class SVGFeMerge : public SVGFe {
 public:
  static std::shared_ptr<SVGFeMerge> Make() {
    return std::shared_ptr<SVGFeMerge>(new SVGFeMerge());
  }

 protected:
  std::shared_ptr<ImageFilter> onMakeImageFilter(
      const SVGRenderContext& context, const SVGFilterContext& filterContext) const override;

  std::vector<SVGFeInputType> getInputs() const override;

 private:
  SVGFeMerge() : INHERITED(SVGTag::FeMerge) {
  }

  using INHERITED = SVGFe;
};

}  // namespace tgfx
