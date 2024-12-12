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
#include <unordered_map>
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Rect.h"
#include "tgfx/svg/SVGRenderContext.h"
#include "tgfx/svg/SVGTypes.h"

namespace tgfx {

class SkSVGFilterContext {
 public:
  SkSVGFilterContext(const Rect& filterEffectsRegion,
                     const SVGObjectBoundingBoxUnits& primitiveUnits)
      : fFilterEffectsRegion(filterEffectsRegion), fPrimitiveUnits(primitiveUnits),
        fPreviousResult({nullptr, filterEffectsRegion, SVGColorspace::SRGB}) {
  }

  const Rect& filterEffectsRegion() const {
    return fFilterEffectsRegion;
  }

  const Rect& filterPrimitiveSubregion(const SVGFeInputType&) const;

  const SVGObjectBoundingBoxUnits& primitiveUnits() const {
    return fPrimitiveUnits;
  }

  void registerResult(const SVGStringType&, const std::shared_ptr<ImageFilter>&, const Rect&,
                      SVGColorspace);

  void setPreviousResult(const std::shared_ptr<ImageFilter>&, const Rect&, SVGColorspace);

  bool previousResultIsSourceGraphic() const;

#ifndef RENDER_SVG
  SVGColorspace resolveInputColorspace(const SVGRenderContext&, const SVGFeInputType&) const;

  std::shared_ptr<ImageFilter> resolveInput(const SVGRenderContext&, const SVGFeInputType&) const;

  std::shared_ptr<ImageFilter> resolveInput(const SVGRenderContext&, const SVGFeInputType&,
                                            SVGColorspace) const;
#endif

 private:
  struct Result {
    std::shared_ptr<ImageFilter> fImageFilter;
    Rect fFilterSubregion;
    SVGColorspace fColorspace;
  };

  const Result* findResultById(const SVGStringType&) const;

#ifndef RENDER_SVG
  std::tuple<std::shared_ptr<ImageFilter>, SVGColorspace> getInput(const SVGRenderContext&,
                                                                   const SVGFeInputType&) const;
#endif

  Rect fFilterEffectsRegion;

  SVGObjectBoundingBoxUnits fPrimitiveUnits;

  std::unordered_map<SVGStringType, Result> fResults;

  Result fPreviousResult;
};

}  // namespace tgfx