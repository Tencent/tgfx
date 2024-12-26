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

#include "tgfx/svg/node/SVGImage.h"
#include <memory>
#include "core/utils/Log.h"
#include "svg/SVGAttributeParser.h"
#include "svg/SVGRenderContext.h"
#include "svg/SVGUtils.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

bool SVGImage::parseAndSetAttribute(const std::string& n, const std::string& v) {
  return INHERITED::parseAndSetAttribute(n, v) ||
         this->setX(SVGAttributeParser::parse<SVGLength>("x", n, v)) ||
         this->setY(SVGAttributeParser::parse<SVGLength>("y", n, v)) ||
         this->setWidth(SVGAttributeParser::parse<SVGLength>("width", n, v)) ||
         this->setHeight(SVGAttributeParser::parse<SVGLength>("height", n, v)) ||
         this->setHref(SVGAttributeParser::parse<SVGIRI>("xlink:href", n, v)) ||
         this->setPreserveAspectRatio(
             SVGAttributeParser::parse<SVGPreserveAspectRatio>("preserveAspectRatio", n, v));
}

bool SVGImage::onPrepareToRender(SVGRenderContext* context) const {
  // Width or height of 0 disables rendering per spec:
  // https://www.w3.org/TR/SVG11/struct.html#ImageElement
  return !Href.iri().empty() && Width.value() > 0 && Height.value() > 0 &&
         INHERITED::onPrepareToRender(context);
}

std::shared_ptr<Image> LoadImage(const SVGIRI& href) {
  const auto& base64URL = href.iri();
  auto pos = base64URL.find("base64,");
  if (pos == std::string::npos) {
    return nullptr;
  }
  std::string base64Data = base64URL.substr(pos + 7);
  auto data = Base64Decode(base64Data);
  return data ? Image::MakeFromEncoded(data) : nullptr;
}

SVGImage::ImageInfo SVGImage::LoadImage(const SVGIRI& iri, const Rect& viewPort,
                                        SVGPreserveAspectRatio /*par*/) {
  std::shared_ptr<Image> image = ::tgfx::LoadImage(iri);
  if (!image) {
    return {};
  }

  // Per spec: raster content has implicit viewbox of '0 0 width height'.
  const Rect viewBox = Rect::MakeWH(image->width(), image->height());

  // Map and place at x, y specified by viewport
  // const Matrix m = ComputeViewboxMatrix(viewBox, viewPort, par);
  // const Rect dst = m.mapRect(viewBox).makeOffset(viewPort.left, viewPort.top);
  Rect dst = viewBox.makeOffset(viewPort.left, viewPort.top);

  return {std::move(image), dst};
}

void SVGImage::onRender(const SVGRenderContext& context) const {
  // Per spec: x, w, width, height attributes establish the new viewport.
  const SVGLengthContext& lengthContext = context.lengthContext();
  const Rect viewPort = lengthContext.resolveRect(X, Y, Width, Height);

  ImageInfo image;
  const auto imgInfo = LoadImage(Href, viewPort, PreserveAspectRatio);
  if (!imgInfo.fImage) {
    LOGE("can't render image: load image failed\n");
    return;
  }

  auto matrix = Matrix::MakeScale(viewPort.width() / imgInfo.fDst.width(),
                                  viewPort.height() / imgInfo.fDst.height());
  matrix.preTranslate(imgInfo.fDst.x(), imgInfo.fDst.y());

  context.canvas()->drawImage(imgInfo.fImage, matrix);
}

Path SVGImage::onAsPath(const SVGRenderContext&) const {
  return {};
}

Rect SVGImage::onObjectBoundingBox(const SVGRenderContext& context) const {
  const SVGLengthContext& lengthContext = context.lengthContext();
  return lengthContext.resolveRect(X, Y, Width, Height);
}

}  // namespace tgfx