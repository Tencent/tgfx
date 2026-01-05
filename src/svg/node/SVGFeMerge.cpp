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

#include "tgfx/svg/node/SVGFeMerge.h"
#include <memory>
#include "svg/SVGAttributeParser.h"
#include "svg/SVGFilterContext.h"
#include "tgfx/core/ImageFilter.h"

namespace tgfx {

// class SVGRenderContext;

bool SVGFeMergeNode::parseAndSetAttribute(const std::string& name, const std::string& value) {
  return INHERITED::parseAndSetAttribute(name, value) ||
         this->setIn(SVGAttributeParser::parse<SVGFeInputType>("in", name, value));
}

std::shared_ptr<ImageFilter> SVGFeMerge::onMakeImageFilter(
    const SVGRenderContext& context, const SVGFilterContext& filterContext) const {
  std::vector<std::shared_ptr<ImageFilter>> mergeNodeFilters(children.size());

  this->forEachChild<SVGFeMergeNode>([&](const SVGFeMergeNode* child) {
    mergeNodeFilters.push_back(filterContext.resolveInput(context, child->getIn()));
  });
  return ImageFilter::Compose(mergeNodeFilters);
}

std::vector<SVGFeInputType> SVGFeMerge::getInputs() const {
  std::vector<SVGFeInputType> inputs;
  inputs.reserve(children.size());

  this->forEachChild<SVGFeMergeNode>(
      [&](const SVGFeMergeNode* child) { inputs.push_back(child->getIn()); });

  return inputs;
}
}  // namespace tgfx
