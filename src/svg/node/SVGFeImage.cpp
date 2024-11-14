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

#include "tgfx/svg/node/SVGFeImage.h"
#include "tgfx/core/Rect.h"
#include "tgfx/svg/SVGAttributeParser.h"

namespace tgfx {

bool SkSVGFeImage::parseAndSetAttribute(const char* n, const char* v) {
  return INHERITED::parseAndSetAttribute(n, v) ||
         this->setHref(SVGAttributeParser::parse<SVGIRI>("xlink:href", n, v)) ||
         this->setPreserveAspectRatio(
             SVGAttributeParser::parse<SVGPreserveAspectRatio>("preserveAspectRatio", n, v));
}

#ifndef RENDER_SVG
std::shared_ptr<ImageFilter> SkSVGFeImage::onMakeImageFilter(
    const SVGRenderContext& /*ctx*/, const SkSVGFilterContext& /*fctx*/) const {
  // // Load image and map viewbox (image bounds) to viewport (filter effects subregion).
  // const Rect viewport = this->resolveFilterSubregion(ctx, fctx);
  // const auto imgInfo =
  //     SVGImage::LoadImage(ctx.resourceProvider(), fHref, viewport, fPreserveAspectRatio);
  // if (!imgInfo.fImage) {
  //   return nullptr;
  // }

  // // Create the image filter mapped according to aspect ratio
  // const Rect srcRect = Rect::Make(imgInfo.fImage->bounds());
  // const Rect& dstRect = imgInfo.fDst;
  // // TODO: image-rendering property
  // auto imgfilt =
  //     ImageFilter:: ::Image(imgInfo.fImage, srcRect, dstRect,
  //                           SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kNearest));

  // // Aspect ratio mapping may end up drawing content outside of the filter effects region,
  // // so perform an explicit crop.
  // return SkImageFilters::Merge(&imgfilt, 1, fctx.filterEffectsRegion());
  return nullptr;
}
#endif
}  // namespace tgfx