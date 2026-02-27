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
#include "tgfx/core/ImageFilter.h"
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/node/SVGFe.h"
#include "tgfx/svg/node/SVGNode.h"

namespace tgfx {

class SVGFeDistantLight;
class SVGFePointLight;
class SVGFeSpotLight;

class SVGFeLighting : public SVGFe {
 public:
  struct KernelUnitLength {
    SVGNumberType Dx;
    SVGNumberType Dy;
  };

  SVG_ATTR(SurfaceScale, SVGNumberType, 1)
  SVG_OPTIONAL_ATTR(UnitLength, KernelUnitLength)

 protected:
  explicit SVGFeLighting(SVGTag t) : INHERITED(t) {
  }

  std::vector<SVGFeInputType> getInputs() const final {
    return {this->getIn()};
  }

  bool parseAndSetAttribute(const std::string& name, const std::string& value) override;

  std::shared_ptr<ImageFilter> onMakeImageFilter(
      const SVGRenderContext& /*context*/, const SVGFilterContext& /*filterContext*/) const final {
    return nullptr;
  };

 private:
  using INHERITED = SVGFe;
};

class SVGFeSpecularLighting final : public SVGFeLighting {
 public:
  static std::shared_ptr<SVGFeSpecularLighting> Make() {
    return std::shared_ptr<SVGFeSpecularLighting>(new SVGFeSpecularLighting());
  }

  SVG_ATTR(SpecularConstant, SVGNumberType, 1)
  SVG_ATTR(SpecularExponent, SVGNumberType, 1)

 protected:
  bool parseAndSetAttribute(const std::string& name, const std::string& value) override;

 private:
  SVGFeSpecularLighting() : INHERITED(SVGTag::FeSpecularLighting) {
  }

  using INHERITED = SVGFeLighting;
};

class SVGFeDiffuseLighting final : public SVGFeLighting {
 public:
  static std::shared_ptr<SVGFeDiffuseLighting> Make() {
    return std::shared_ptr<SVGFeDiffuseLighting>(new SVGFeDiffuseLighting());
  }

  SVG_ATTR(DiffuseConstant, SVGNumberType, 1)

 protected:
  bool parseAndSetAttribute(const std::string& name, const std::string& value) override;

 private:
  SVGFeDiffuseLighting() : INHERITED(SVGTag::FeDiffuseLighting) {
  }

  using INHERITED = SVGFeLighting;
};

}  // namespace tgfx
