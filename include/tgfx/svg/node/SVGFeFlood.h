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

class SkImageFilter;
class SkSVGFilterContext;
class SVGRenderContext;

class SkSVGFeFlood : public SkSVGFe {
 public:
  static std::shared_ptr<SkSVGFeFlood> Make() {
    return std::shared_ptr<SkSVGFeFlood>(new SkSVGFeFlood());
  }

 protected:
  std::shared_ptr<ImageFilter> onMakeImageFilter(const SVGRenderContext&,
                                                 const SkSVGFilterContext&) const override {
    return nullptr;
  };
#ifdef RENDER_SVG
  sk_sp<SkImageFilter> onMakeImageFilter(const SVGRenderContext&,
                                         const SkSVGFilterContext&) const override;
#endif

  std::vector<SVGFeInputType> getInputs() const override {
    return {};
  }

 private:
  SkSVGFeFlood() : INHERITED(SVGTag::FeFlood) {
  }
#ifdef RENDER_SVG
  SkColor resolveFloodColor(const SVGRenderContext&) const;
#endif

  using INHERITED = SkSVGFe;
};
}  // namespace tgfx
