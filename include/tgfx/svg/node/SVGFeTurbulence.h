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

class SkSVGFeTurbulence : public SkSVGFe {
 public:
  static std::shared_ptr<SkSVGFeTurbulence> Make() {
    return std::shared_ptr<SkSVGFeTurbulence>(new SkSVGFeTurbulence());
  }

  SVG_ATTR(BaseFrequency, SVGFeTurbulenceBaseFrequency, SVGFeTurbulenceBaseFrequency({}))
  SVG_ATTR(NumOctaves, SVGIntegerType, SVGIntegerType(1))
  SVG_ATTR(Seed, SVGNumberType, SVGNumberType(0))
  SVG_ATTR(TurbulenceType, SVGFeTurbulenceType,
           SVGFeTurbulenceType(SVGFeTurbulenceType::Type::kTurbulence))

 protected:
#ifndef RENDER_SVG
  std::shared_ptr<ImageFilter> onMakeImageFilter(const SVGRenderContext&,
                                                 const SkSVGFilterContext&) const override;
#endif

  std::vector<SVGFeInputType> getInputs() const override {
    return {};
  }

  bool parseAndSetAttribute(const char*, const char*) override;

 private:
  SkSVGFeTurbulence() : INHERITED(SVGTag::FeTurbulence) {
  }

  using INHERITED = SkSVGFe;
};

}  // namespace tgfx
