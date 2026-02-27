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
#include "tgfx/svg/node/SVGNode.h"

namespace tgfx {

class SkImageFilter;
class SVGFilterContext;
class SVGRenderContext;

class SVGFeOffset : public SVGFe {
 public:
  static std::shared_ptr<SVGFeOffset> Make() {
    return std::shared_ptr<SVGFeOffset>(new SVGFeOffset());
  }

  SVG_ATTR(Dx, SVGNumberType, SVGNumberType(0))
  SVG_ATTR(Dy, SVGNumberType, SVGNumberType(0))

 protected:
  std::shared_ptr<ImageFilter> onMakeImageFilter(
      const SVGRenderContext& context, const SVGFilterContext& filterContext) const override;

  std::vector<SVGFeInputType> getInputs() const override {
    return {this->getIn()};
  }

  bool parseAndSetAttribute(const std::string& name, const std::string& value) override;

 private:
  SVGFeOffset() : INHERITED(SVGTag::FeOffset) {
  }

  using INHERITED = SVGFe;
};
}  // namespace tgfx
