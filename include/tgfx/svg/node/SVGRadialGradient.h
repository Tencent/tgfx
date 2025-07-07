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
#include "tgfx/core/Matrix.h"
#include "tgfx/core/TileMode.h"
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/node/SVGGradient.h"
#include "tgfx/svg/node/SVGNode.h"

namespace tgfx {

class SVGRadialGradient final : public SVGGradient {
 public:
  static std::shared_ptr<SVGRadialGradient> Make() {
    return std::shared_ptr<SVGRadialGradient>(new SVGRadialGradient());
  }

  SVG_ATTR(Cx, SVGLength, SVGLength(50, SVGLength::Unit::Percentage))
  SVG_ATTR(Cy, SVGLength, SVGLength(50, SVGLength::Unit::Percentage))
  SVG_ATTR(R, SVGLength, SVGLength(50, SVGLength::Unit::Percentage))
  SVG_OPTIONAL_ATTR(Fx, SVGLength)
  SVG_OPTIONAL_ATTR(Fy, SVGLength)

 protected:
  bool parseAndSetAttribute(const std::string& name, const std::string& value) override;

  std::shared_ptr<Shader> onMakeShader(const SVGRenderContext& context,
                                       const std::vector<Color>& colors,
                                       const std::vector<float>& positions, TileMode tileMode,
                                       const Matrix& localMatrix) const override;

 private:
  SVGRadialGradient();

  using INHERITED = SVGGradient;
};
}  // namespace tgfx
