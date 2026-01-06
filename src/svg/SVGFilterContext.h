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
#include <unordered_map>
#include "svg/SVGRenderContext.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Rect.h"
#include "tgfx/svg/SVGTypes.h"

namespace tgfx {

class SVGFilterContext {
 public:
  SVGFilterContext(const Rect& filterEffectsRegion, const SVGObjectBoundingBoxUnits& primitiveUnits)
      : _filterEffectsRegion(filterEffectsRegion), _primitiveUnits(primitiveUnits),
        previousResult({nullptr, filterEffectsRegion, SVGColorspace::SRGB}) {
  }

  const Rect& filterEffectsRegion() const {
    return _filterEffectsRegion;
  }

  const Rect& filterPrimitiveSubregion(const SVGFeInputType& input) const;

  const SVGObjectBoundingBoxUnits& primitiveUnits() const {
    return _primitiveUnits;
  }

  void registerResult(const SVGStringType& ID, const std::shared_ptr<ImageFilter>& result,
                      const Rect& subregion, SVGColorspace resultColorspace);

  void setPreviousResult(const std::shared_ptr<ImageFilter>& result, const Rect& subregion,
                         SVGColorspace resultColorspace);

  bool previousResultIsSourceGraphic() const;

  SVGColorspace resolveInputColorspace(const SVGRenderContext& context,
                                       const SVGFeInputType& inputType) const;

  std::shared_ptr<ImageFilter> resolveInput(const SVGRenderContext& context,
                                            const SVGFeInputType& inputType) const;

  std::shared_ptr<ImageFilter> resolveInput(const SVGRenderContext& context,
                                            const SVGFeInputType& inputType,
                                            SVGColorspace resultColorspace) const;

 private:
  struct Result {
    std::shared_ptr<ImageFilter> imageFilter;
    Rect filterSubregion;
    SVGColorspace colorspace;
  };

  const Result* findResultById(const SVGStringType& ID) const;

  std::tuple<std::shared_ptr<ImageFilter>, SVGColorspace> getInput(
      const SVGRenderContext& context, const SVGFeInputType& inputType) const;

  Rect _filterEffectsRegion;

  SVGObjectBoundingBoxUnits _primitiveUnits;

  std::unordered_map<SVGStringType, Result> results;

  Result previousResult;
};

}  // namespace tgfx
