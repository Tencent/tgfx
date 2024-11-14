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

#include "tgfx/svg/node/SVGFeMerge.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/svg/SVGAttributeParser.h"

namespace tgfx {

// class SVGRenderContext;

bool SkSVGFeMergeNode::parseAndSetAttribute(const char* name, const char* value) {
  return INHERITED::parseAndSetAttribute(name, value) ||
         this->setIn(SVGAttributeParser::parse<SVGFeInputType>("in", name, value));
}

#ifndef RENDER_SVG
std::shared_ptr<ImageFilter> SkSVGFeMerge::onMakeImageFilter(
    const SVGRenderContext& /*ctx*/, const SkSVGFilterContext& /*fctx*/) const {
  // const SkSVGColorspace colorspace = this->resolveColorspace(ctx, fctx);

  // skia_private::STArray<8, sk_sp<SkImageFilter>> merge_node_filters;
  // merge_node_filters.reserve(fChildren.size());

  // this->forEachChild<SkSVGFeMergeNode>([&](const SkSVGFeMergeNode* child) {
  //   merge_node_filters.push_back(fctx.resolveInput(ctx, child->getIn(), colorspace));
  // });

  // return SkImageFilters::Merge(merge_node_filters.data(), merge_node_filters.size(),
  //                              this->resolveFilterSubregion(ctx, fctx));
  return nullptr;
}
#endif

std::vector<SVGFeInputType> SkSVGFeMerge::getInputs() const {
  std::vector<SVGFeInputType> inputs;
  inputs.reserve(fChildren.size());

  this->forEachChild<SkSVGFeMergeNode>(
      [&](const SkSVGFeMergeNode* child) { inputs.push_back(child->getIn()); });

  return inputs;
}
}  // namespace tgfx