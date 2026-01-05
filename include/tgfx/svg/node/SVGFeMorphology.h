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

class SVGFeMorphology : public SVGFe {
 public:
  struct Radius {
    SVGNumberType fX;
    SVGNumberType fY;
  };

  enum class Operator {
    kErode,
    kDilate,
  };

  static std::shared_ptr<SVGFeMorphology> Make() {
    return std::shared_ptr<SVGFeMorphology>(new SVGFeMorphology());
  }

  SVG_ATTR(MorphOperator, Operator, Operator::kErode)
  SVG_ATTR(MorphRadius, Radius, Radius({0, 0}))

 protected:
  std::shared_ptr<ImageFilter> onMakeImageFilter(
      const SVGRenderContext& context, const SVGFilterContext& filterContext) const override;

  std::vector<SVGFeInputType> getInputs() const override {
    return {this->getIn()};
  }

  bool parseAndSetAttribute(const std::string& name, const std::string& value) override;

 private:
  SVGFeMorphology() : INHERITED(SVGTag::FeMorphology) {
  }

  using INHERITED = SVGFe;
};

}  // namespace tgfx
