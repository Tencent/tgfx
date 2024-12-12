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

class SVGMask final : public SVGHiddenContainer {
 public:
  static std::shared_ptr<SVGMask> Make() {
    return std::shared_ptr<SVGMask>(new SVGMask());
  }

  SVG_OPTIONAL_ATTR(X, SVGLength)
  SVG_OPTIONAL_ATTR(Y, SVGLength)
  SVG_OPTIONAL_ATTR(Width, SVGLength)
  SVG_OPTIONAL_ATTR(Height, SVGLength)

  SVG_ATTR(MaskUnits, SVGObjectBoundingBoxUnits,
           SVGObjectBoundingBoxUnits(SVGObjectBoundingBoxUnits::Type::ObjectBoundingBox))
  SVG_ATTR(MaskContentUnits, SVGObjectBoundingBoxUnits,
           SVGObjectBoundingBoxUnits(SVGObjectBoundingBoxUnits::Type::UserSpaceOnUse))

 private:
  friend class SVGRenderContext;

  SVGMask() : INHERITED(SVGTag::Mask) {
  }

  bool parseAndSetAttribute(const std::string&, const std::string&) override;

  Rect bounds(const SVGRenderContext&) const;

  void renderMask(const SVGRenderContext&) const;

  using INHERITED = SVGHiddenContainer;
};

}  // namespace tgfx
