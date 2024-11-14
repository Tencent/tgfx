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
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/node/SVGHiddenContainer.h"
#include "tgfx/svg/node/SVGNode.h"

namespace tgfx {

class SkSVGFeLightSource : public SVGHiddenContainer {
 public:
  void appendChild(std::shared_ptr<SVGNode>) final {
  }

 protected:
  explicit SkSVGFeLightSource(SVGTag tag) : INHERITED(tag) {
  }

 private:
  using INHERITED = SVGHiddenContainer;
};

class SkSVGFeDistantLight final : public SkSVGFeLightSource {
 public:
  static std::shared_ptr<SkSVGFeDistantLight> Make() {
    return std::shared_ptr<SkSVGFeDistantLight>(new SkSVGFeDistantLight());
  }

  //   pk::SkPoint3 computeDirection() const;

  SVG_ATTR(Azimuth, SVGNumberType, 0)
  SVG_ATTR(Elevation, SVGNumberType, 0)

 private:
  SkSVGFeDistantLight() : INHERITED(SVGTag::kFeDistantLight) {
  }

  bool parseAndSetAttribute(const char*, const char*) override;

  using INHERITED = SkSVGFeLightSource;
};

class SkSVGFePointLight final : public SkSVGFeLightSource {
 public:
  static std::shared_ptr<SkSVGFePointLight> Make() {
    return std::shared_ptr<SkSVGFePointLight>(new SkSVGFePointLight());
  }

  SVG_ATTR(X, SVGNumberType, 0)
  SVG_ATTR(Y, SVGNumberType, 0)
  SVG_ATTR(Z, SVGNumberType, 0)

 private:
  SkSVGFePointLight() : INHERITED(SVGTag::kFePointLight) {
  }

  bool parseAndSetAttribute(const char*, const char*) override;

  using INHERITED = SkSVGFeLightSource;
};

class SkSVGFeSpotLight final : public SkSVGFeLightSource {
 public:
  static std::shared_ptr<SkSVGFeSpotLight> Make() {
    return std::shared_ptr<SkSVGFeSpotLight>(new SkSVGFeSpotLight());
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
  SkSVGFeSpotLight() : INHERITED(SVGTag::kFeSpotLight) {
  }

  bool parseAndSetAttribute(const char*, const char*) override;

  using INHERITED = SkSVGFeLightSource;
};

}  // namespace tgfx