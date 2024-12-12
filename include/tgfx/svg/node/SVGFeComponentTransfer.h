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

#include <__memory/shared_ptr.h>
#include <cstdint>
#include <memory>
#include <vector>
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/node/SVGFe.h"
#include "tgfx/svg/node/SVGHiddenContainer.h"
#include "tgfx/svg/node/SVGNode.h"

class SkSVGFilterContext;
class SVGRenderContext;

namespace tgfx {

class SkSVGFeFunc final : public SVGHiddenContainer {
 public:
  static std::shared_ptr<SkSVGFeFunc> MakeFuncA() {
    return std::shared_ptr<SkSVGFeFunc>(new SkSVGFeFunc(SVGTag::FeFuncA));
  }

  static std::shared_ptr<SkSVGFeFunc> MakeFuncR() {
    return std::shared_ptr<SkSVGFeFunc>(new SkSVGFeFunc(SVGTag::FeFuncR));
  }

  static std::shared_ptr<SkSVGFeFunc> MakeFuncG() {
    return std::shared_ptr<SkSVGFeFunc>(new SkSVGFeFunc(SVGTag::FeFuncG));
  }

  static std::shared_ptr<SkSVGFeFunc> MakeFuncB() {
    return std::shared_ptr<SkSVGFeFunc>(new SkSVGFeFunc(SVGTag::FeFuncB));
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
  bool parseAndSetAttribute(const char*, const char*) override;

 private:
  SkSVGFeFunc(SVGTag tag) : INHERITED(tag) {
  }

  using INHERITED = SVGHiddenContainer;
};

class SkSVGFeComponentTransfer final : public SkSVGFe {
 public:
  static std::shared_ptr<SkSVGFeComponentTransfer> Make() {
    return std::shared_ptr<SkSVGFeComponentTransfer>(new SkSVGFeComponentTransfer());
  }

 protected:
#ifndef RENDER_SVG
  std::shared_ptr<ImageFilter> onMakeImageFilter(const SVGRenderContext&,
                                                 const SkSVGFilterContext&) const override {
    return nullptr;
  };
#endif
  std::vector<SVGFeInputType> getInputs() const override {
    return {this->getIn()};
  }

 private:
  SkSVGFeComponentTransfer() : INHERITED(SVGTag::FeComponentTransfer) {
  }

  using INHERITED = SkSVGFe;
};
}  // namespace tgfx