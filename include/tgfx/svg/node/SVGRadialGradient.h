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
#include "tgfx/core/Matrix.h"
#include "tgfx/core/TileMode.h"
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/node/SVGGradient.h"
#include "tgfx/svg/node/SVGNode.h"

namespace tgfx {

class SkSVGRadialGradient final : public SkSVGGradient {
 public:
  static std::shared_ptr<SkSVGRadialGradient> Make() {
    return std::shared_ptr<SkSVGRadialGradient>(new SkSVGRadialGradient());
  }

  SVG_ATTR(Cx, SVGLength, SVGLength(50, SVGLength::Unit::kPercentage))
  SVG_ATTR(Cy, SVGLength, SVGLength(50, SVGLength::Unit::kPercentage))
  SVG_ATTR(R, SVGLength, SVGLength(50, SVGLength::Unit::kPercentage))
  SVG_OPTIONAL_ATTR(Fx, SVGLength)
  SVG_OPTIONAL_ATTR(Fy, SVGLength)

 protected:
  bool parseAndSetAttribute(const char*, const char*) override;

#ifndef RENDER_SVG
  std::shared_ptr<Shader> onMakeShader(const SVGRenderContext&, const std::vector<Color>&,
                                       const std::vector<float>&, TileMode,
                                       const Matrix&) const override;
#endif

 private:
  SkSVGRadialGradient();

  using INHERITED = SkSVGGradient;
};
}  // namespace tgfx
