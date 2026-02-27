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

#include <cstdint>
#include <memory>
#include <vector>
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/node/SVGFe.h"
#include "tgfx/svg/node/SVGHiddenContainer.h"
#include "tgfx/svg/node/SVGNode.h"

class SVGFilterContext;
class SVGRenderContext;

namespace tgfx {

class SVGFeFunc final : public SVGHiddenContainer {
 public:
  static std::shared_ptr<SVGFeFunc> MakeFuncA() {
    return std::shared_ptr<SVGFeFunc>(new SVGFeFunc(SVGTag::FeFuncA));
  }

  static std::shared_ptr<SVGFeFunc> MakeFuncR() {
    return std::shared_ptr<SVGFeFunc>(new SVGFeFunc(SVGTag::FeFuncR));
  }

  static std::shared_ptr<SVGFeFunc> MakeFuncG() {
    return std::shared_ptr<SVGFeFunc>(new SVGFeFunc(SVGTag::FeFuncG));
  }

  static std::shared_ptr<SVGFeFunc> MakeFuncB() {
    return std::shared_ptr<SVGFeFunc>(new SVGFeFunc(SVGTag::FeFuncB));
  }

  SVG_ATTR(Amplitude, SVGNumberType, 1)
  SVG_ATTR(Exponent, SVGNumberType, 1)
  SVG_ATTR(Intercept, SVGNumberType, 0)
  SVG_ATTR(Offset, SVGNumberType, 0)
  SVG_ATTR(Slope, SVGNumberType, 1)
  SVG_ATTR(TableValues, std::vector<SVGNumberType>, {})
  SVG_ATTR(Type, SVGFeFuncType, SVGFeFuncType::Identity)

  std::vector<uint8_t> getTable() const;

 protected:
  bool parseAndSetAttribute(const std::string&, const std::string&) override;

 private:
  SVGFeFunc(SVGTag tag) : INHERITED(tag) {
  }

  using INHERITED = SVGHiddenContainer;
};

class SVGFeComponentTransfer final : public SVGFe {
 public:
  static std::shared_ptr<SVGFeComponentTransfer> Make() {
    return std::shared_ptr<SVGFeComponentTransfer>(new SVGFeComponentTransfer());
  }

 protected:
  std::shared_ptr<ImageFilter> onMakeImageFilter(
      const SVGRenderContext& /*context*/,
      const SVGFilterContext& /*filterContext*/) const override {
    return nullptr;
  };

  std::vector<SVGFeInputType> getInputs() const override {
    return {this->getIn()};
  }

 private:
  SVGFeComponentTransfer() : INHERITED(SVGTag::FeComponentTransfer) {
  }

  using INHERITED = SVGFe;
};
}  // namespace tgfx
