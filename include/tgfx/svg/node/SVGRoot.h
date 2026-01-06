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
#include "tgfx/core/Size.h"
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/SVGValue.h"
#include "tgfx/svg/node/SVGContainer.h"
#include "tgfx/svg/node/SVGNode.h"

namespace tgfx {

class SVGLengthContext;

class SVGRoot : public SVGContainer {
 public:
  enum class Type {
    kRoot,
    kInner,
  };
  static std::shared_ptr<SVGRoot> Make(Type t = Type::kInner) {
    return std::shared_ptr<SVGRoot>(new SVGRoot(t));
  }

  SVG_ATTR(X, SVGLength, SVGLength(0))
  SVG_ATTR(Y, SVGLength, SVGLength(0))
  SVG_ATTR(Width, SVGLength, SVGLength(100, SVGLength::Unit::Percentage))
  SVG_ATTR(Height, SVGLength, SVGLength(100, SVGLength::Unit::Percentage))
  SVG_ATTR(PreserveAspectRatio, SVGPreserveAspectRatio, SVGPreserveAspectRatio())

  SVG_OPTIONAL_ATTR(ViewBox, SVGViewBoxType)

  Size intrinsicSize(const SVGLengthContext& lengthContext) const;

  void renderNode(const SVGRenderContext& context, const SVGIRI& iri) const;

 protected:
  bool onPrepareToRender(SVGRenderContext* context) const override;

  void onSetAttribute(SVGAttribute attribute, const SVGValue& value) override;

 private:
  explicit SVGRoot(Type t) : INHERITED(SVGTag::Svg), type(t) {
  }

  // Some attributes behave differently for the outermost svg element.
  const Type type;

  using INHERITED = SVGContainer;
};

}  // namespace tgfx
