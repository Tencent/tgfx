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

#include "tgfx/svg/node/SVGImage.h"
#include <filesystem>
#include <memory>
#include "core/utils/Log.h"
#include "svg/SVGAttributeParser.h"
#include "svg/SVGRenderContext.h"
#include "svg/SVGUtils.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Stream.h"
#include "tgfx/svg/SVGTypes.h"

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

  // SVG image URLs support five protocols: http://, https://, file://, path, and base64
  // For http, https, file, and path protocols:
  //   - http, https, and file require a registered StreamFactory
  //   - path protocol requires an absolute path
  //   - all are loaded using Image::MakeFromFile
  // For base64 protocol:
  //   - base64 data is decoded and loaded using Image::MakeFromEncoded

  switch (href.type()) {
    case SVGIRI::Type::DataURI: {
      const auto& base64URL = href.iri();
      auto pos = base64URL.find("base64,");
      if (pos == std::string::npos) {
        return Image::MakeFromFile(base64URL);
      }
      std::string base64Data = base64URL.substr(pos + 7);
      auto data = Base64Decode(base64Data);
      return data ? Image::MakeFromEncoded(data) : nullptr;
    }
    case SVGIRI::Type::Nonlocal: {
      return Image::MakeFromFile(href.iri());
    }
    default:
      return nullptr;
  }
}

SVGImage::ImageInfo SVGImage::LoadImage(const SVGIRI& iri, const Rect& viewPort) {
  std::shared_ptr<Image> image = ::tgfx::LoadImage(iri);
  if (!image) {
    return {};
  }

  // Per spec: raster content has implicit viewbox of '0 0 width height'.
  const Rect viewBox = Rect::MakeWH(image->width(), image->height());

  // Map and place at x, y specified by viewport
  Rect dst = viewBox.makeOffset(viewPort.left, viewPort.top);

  return {std::move(image), dst};
}

void SVGImage::onRender(const SVGRenderContext& context) const {
  // Per spec: x, w, width, height attributes establish the new viewport.
  const SVGLengthContext& lengthContext = context.lengthContext();
  const Rect viewPort = lengthContext.resolveRect(X, Y, Width, Height);

  ImageInfo image;
  const auto imgInfo = LoadImage(Href, viewPort);
  if (!imgInfo.image) {
    LOGE("can't render image: load image failed\n");
    return;
  }

  auto matrix = Matrix::MakeScale(viewPort.width() / imgInfo.destinationRect.width(),
                                  viewPort.height() / imgInfo.destinationRect.height());
  matrix.preTranslate(imgInfo.destinationRect.x(), imgInfo.destinationRect.y());

  context.canvas()->concat(matrix);
  context.canvas()->drawImage(imgInfo.image);
}

Path SVGImage::onAsPath(const SVGRenderContext&) const {
  return {};
}

Rect SVGImage::onObjectBoundingBox(const SVGRenderContext& context) const {
  const SVGLengthContext& lengthContext = context.lengthContext();
  return lengthContext.resolveRect(X, Y, Width, Height);
}

}  // namespace tgfx
