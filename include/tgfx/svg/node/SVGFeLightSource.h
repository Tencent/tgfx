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
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/node/SVGHiddenContainer.h"
#include "tgfx/svg/node/SVGNode.h"

namespace tgfx {

class SVGFeLightSource : public SVGHiddenContainer {
 public:
  void appendChild(std::shared_ptr<SVGNode>) final {
  }

 protected:
  explicit SVGFeLightSource(SVGTag tag) : INHERITED(tag) {
  }

 private:
  using INHERITED = SVGHiddenContainer;
};

class SVGFeDistantLight final : public SVGFeLightSource {
 public:
  static std::shared_ptr<SVGFeDistantLight> Make() {
    return std::shared_ptr<SVGFeDistantLight>(new SVGFeDistantLight());
  }

  //   pk::SkPoint3 computeDirection() const;

  SVG_ATTR(Azimuth, SVGNumberType, 0)
  SVG_ATTR(Elevation, SVGNumberType, 0)

 private:
  SVGFeDistantLight() : INHERITED(SVGTag::FeDistantLight) {
  }

  bool parseAndSetAttribute(const std::string& name, const std::string& value) override;

  using INHERITED = SVGFeLightSource;
};

class SVGFePointLight final : public SVGFeLightSource {
 public:
  static std::shared_ptr<SVGFePointLight> Make() {
    return std::shared_ptr<SVGFePointLight>(new SVGFePointLight());
  }

  SVG_ATTR(X, SVGNumberType, 0)
  SVG_ATTR(Y, SVGNumberType, 0)
  SVG_ATTR(Z, SVGNumberType, 0)

 private:
  SVGFePointLight() : INHERITED(SVGTag::FePointLight) {
  }

  bool parseAndSetAttribute(const std::string& name, const std::string& value) override;

  using INHERITED = SVGFeLightSource;
};

class SVGFeSpotLight final : public SVGFeLightSource {
 public:
  static std::shared_ptr<SVGFeSpotLight> Make() {
    return std::shared_ptr<SVGFeSpotLight>(new SVGFeSpotLight());
  }

  SVG_ATTR(X, SVGNumberType, 0)
  SVG_ATTR(Y, SVGNumberType, 0)
  SVG_ATTR(Z, SVGNumberType, 0)
  SVG_ATTR(PointsAtX, SVGNumberType, 0)
  SVG_ATTR(PointsAtY, SVGNumberType, 0)
  SVG_ATTR(PointsAtZ, SVGNumberType, 0)
  SVG_ATTR(SpecularExponent, SVGNumberType, 1)

  SVG_OPTIONAL_ATTR(LimitingConeAngle, SVGNumberType)

 private:
  SVGFeSpotLight() : INHERITED(SVGTag::FeSpotLight) {
  }

  bool parseAndSetAttribute(const std::string& name, const std::string& value) override;

  using INHERITED = SVGFeLightSource;
};

}  // namespace tgfx
