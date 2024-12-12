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
#include "tgfx/core/Shader.h"
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/node/SVGGradient.h"
#include "tgfx/svg/node/SVGNode.h"

namespace tgfx {

class SVGLinearGradient final : public SVGGradient {
 public:
  static std::shared_ptr<SVGLinearGradient> Make() {
    return std::shared_ptr<SVGLinearGradient>(new SVGLinearGradient());
  }

  SVG_ATTR(X1, SVGLength, SVGLength(0, SVGLength::Unit::Percentage))
  SVG_ATTR(Y1, SVGLength, SVGLength(0, SVGLength::Unit::Percentage))
  SVG_ATTR(X2, SVGLength, SVGLength(100, SVGLength::Unit::Percentage))
  SVG_ATTR(Y2, SVGLength, SVGLength(0, SVGLength::Unit::Percentage))

 protected:
  bool parseAndSetAttribute(const std::string&, const std::string&) override;

  std::shared_ptr<Shader> onMakeShader(const SVGRenderContext&, const std::vector<Color>&,
                                       const std::vector<float>&, TileMode,
                                       const Matrix& localMatrix) const override;

 private:
  SVGLinearGradient();

  using INHERITED = SVGGradient;
};
}  // namespace tgfx
