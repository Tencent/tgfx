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
#include "tgfx/core/Path.h"
#include "tgfx/core/Rect.h"
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/node/SVGNode.h"
#include "tgfx/svg/node/SVGTransformableNode.h"

namespace tgfx {

class SVGRenderContext;

/**
 * Implements support for <use> (reference) elements.
 * (https://www.w3.org/TR/SVG11/struct.html#UseElement)
 */
class SVGUse final : public SVGTransformableNode {
 public:
  static std::shared_ptr<SVGUse> Make() {
    return std::shared_ptr<SVGUse>(new SVGUse());
  }

  void appendChild(std::shared_ptr<SVGNode>) override{};

  SVG_ATTR(X, SVGLength, SVGLength(0))
  SVG_ATTR(Y, SVGLength, SVGLength(0))
  SVG_ATTR(Href, SVGIRI, SVGIRI())

 protected:
  bool onPrepareToRender(SVGRenderContext* context) const override;
  void onRender(const SVGRenderContext& context) const override;
  Path onAsPath(const SVGRenderContext& context) const override;
  Rect onObjectBoundingBox(const SVGRenderContext& context) const override;

 private:
  SVGUse();

  bool parseAndSetAttribute(const std::string& name, const std::string& value) override;

  using INHERITED = SVGTransformableNode;
};
}  // namespace tgfx
