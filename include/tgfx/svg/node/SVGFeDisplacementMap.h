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
#include <vector>
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/node/SVGFe.h"
#include "tgfx/svg/node/SVGNode.h"

namespace tgfx {

class SkSVGFilterContext;
class SVGRenderContext;

class SkSVGFeDisplacementMap : public SkSVGFe {
 public:
  using ChannelSelector = ColorChannel;

  static std::shared_ptr<SkSVGFeDisplacementMap> Make() {
    return std::shared_ptr<SkSVGFeDisplacementMap>(new SkSVGFeDisplacementMap());
  }

  SVG_ATTR(In2, SVGFeInputType, SVGFeInputType())
  SVG_ATTR(XChannelSelector, ChannelSelector, ChannelSelector::A)
  SVG_ATTR(YChannelSelector, ChannelSelector, ChannelSelector::A)
  SVG_ATTR(Scale, SVGNumberType, SVGNumberType(0))

 protected:
  std::vector<SVGFeInputType> getInputs() const override {
    return {this->getIn(), this->getIn2()};
  }

  bool parseAndSetAttribute(const char*, const char*) override;

 private:
  SkSVGFeDisplacementMap() : INHERITED(SVGTag::kFeDisplacementMap) {
  }

  using INHERITED = SkSVGFe;

#ifndef RENDER_SVG
 public:
  SVGColorspace resolveColorspace(const SVGRenderContext&, const SkSVGFilterContext&) const final;

 protected:
  std::shared_ptr<ImageFilter> onMakeImageFilter(const SVGRenderContext&,
                                                 const SkSVGFilterContext&) const override;
#endif
};

}  // namespace tgfx