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
#include "tgfx/core/Rect.h"
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/node/SVGHiddenContainer.h"
#include "tgfx/svg/node/SVGNode.h"

namespace tgfx {

class SVGFilterContext;
class SVGFe : public SVGHiddenContainer {
 public:
  static bool IsFilterEffect(const std::shared_ptr<SVGNode>& node) {
    switch (node->tag()) {
      case SVGTag::FeBlend:
      case SVGTag::FeColorMatrix:
      case SVGTag::FeComponentTransfer:
      case SVGTag::FeComposite:
      case SVGTag::FeDiffuseLighting:
      case SVGTag::FeDisplacementMap:
      case SVGTag::FeFlood:
      case SVGTag::FeGaussianBlur:
      case SVGTag::FeImage:
      case SVGTag::FeMerge:
      case SVGTag::FeMorphology:
      case SVGTag::FeOffset:
      case SVGTag::FeSpecularLighting:
      case SVGTag::FeTurbulence:
        return true;
      default:
        return false;
    }
  }

  std::shared_ptr<ImageFilter> makeImageFilter(const SVGRenderContext& context,
                                               const SVGFilterContext& filterContext) const;

  // https://www.w3.org/TR/SVG11/filters.html#FilterPrimitiveSubRegion
  Rect resolveFilterSubregion(const SVGRenderContext& context,
                              const SVGFilterContext& filterContext) const;

  /**
   * Resolves the colorspace within which this filter effect should be applied.
   * Spec: https://www.w3.org/TR/SVG11/painting.html#ColorInterpolationProperties
   * 'color-interpolation-filters' property.
   */
  virtual SVGColorspace resolveColorspace(const SVGRenderContext& context,
                                          const SVGFilterContext& filterContext) const;

  /** Propagates any inherited presentation attributes in the given context. */
  void applyProperties(SVGRenderContext* context) const;

  SVG_ATTR(In, SVGFeInputType, SVGFeInputType())

  SVG_ATTR(Result, SVGStringType, SVGStringType())
  SVG_OPTIONAL_ATTR(X, SVGLength)
  SVG_OPTIONAL_ATTR(Y, SVGLength)
  SVG_OPTIONAL_ATTR(Width, SVGLength)
  SVG_OPTIONAL_ATTR(Height, SVGLength)

 protected:
  explicit SVGFe(SVGTag t) : INHERITED(t) {
  }

  virtual std::shared_ptr<ImageFilter> onMakeImageFilter(
      const SVGRenderContext& context, const SVGFilterContext& filterContext) const = 0;

  virtual std::vector<SVGFeInputType> getInputs() const = 0;

  bool parseAndSetAttribute(const std::string& name, const std::string& value) override;

 private:
  /**
     * Resolves the rect specified by the x, y, width and height attributes (if specified) on this
     * filter effect. These attributes are resolved according to the given length context and
     * the value of 'primitiveUnits' on the parent <filter> element.
     */
  Rect resolveBoundaries(const SVGRenderContext& context,
                         const SVGFilterContext& filterContext) const;

  using INHERITED = SVGHiddenContainer;
};
}  // namespace tgfx
