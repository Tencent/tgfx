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

#include "tgfx/core/Size.h"
#include "tgfx/svg/SVGTypes.h"

namespace tgfx {

class SVGLengthContext {
 public:
  explicit SVGLengthContext(const Size& viewport, float dpi = 90) : _viewPort(viewport), dpi(dpi) {
  }

  enum class LengthType {
    Horizontal,
    Vertical,
    Other,
  };

  const Size& viewPort() const {
    return _viewPort;
  }
  void setViewPort(const Size& viewPort) {
    _viewPort = viewPort;
  }

  float resolve(const SVGLength&, LengthType) const;

  Rect resolveRect(const SVGLength& x, const SVGLength& y, const SVGLength& w,
                   const SVGLength& h) const;

  std::tuple<float, float> resolveOptionalRadii(const std::optional<SVGLength>& optionalRx,
                                                const std::optional<SVGLength>& optionalRy) const;

  void setPatternUnits(SVGObjectBoundingBoxUnits unit) {
    patternUnit = unit;
  }

  void clearPatternUnits() {
    patternUnit.reset();
  }

  std::optional<SVGObjectBoundingBoxUnits> getPatternUnits() const {
    return patternUnit;
  }

 private:
  Size _viewPort;
  float dpi;
  std::optional<SVGObjectBoundingBoxUnits> patternUnit;
};

}  // namespace tgfx