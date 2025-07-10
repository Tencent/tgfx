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

#include <memory>
#include <vector>
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/node/SVGFe.h"
#include "tgfx/svg/node/SVGNode.h"

namespace tgfx {

class SVGFilterContext;
class SVGRenderContext;

class SVGFeComposite final : public SVGFe {
 public:
  static std::shared_ptr<SVGFeComposite> Make() {
    return std::shared_ptr<SVGFeComposite>(new SVGFeComposite());
  }

  SVG_ATTR(In2, SVGFeInputType, SVGFeInputType())
  SVG_ATTR(K1, SVGNumberType, SVGNumberType(0))
  SVG_ATTR(K2, SVGNumberType, SVGNumberType(0))
  SVG_ATTR(K3, SVGNumberType, SVGNumberType(0))
  SVG_ATTR(K4, SVGNumberType, SVGNumberType(0))
  SVG_ATTR(Operator, SVGFeCompositeOperator, SVGFeCompositeOperator::Over)

 protected:
  std::vector<SVGFeInputType> getInputs() const override {
    return {this->getIn(), this->getIn2()};
  }

  bool parseAndSetAttribute(const std::string& name, const std::string& value) override;

  std::shared_ptr<ImageFilter> onMakeImageFilter(
      const SVGRenderContext& context, const SVGFilterContext& filterContext) const override;

 private:
  SVGFeComposite() : INHERITED(SVGTag::FeComposite) {
  }

  static BlendMode BlendModeForOperator(SVGFeCompositeOperator op);

  using INHERITED = SVGFe;
};
}  // namespace tgfx
