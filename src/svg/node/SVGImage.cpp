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

std::vector<unsigned char> base64_decode(const std::string& encoded_string) {
  static constexpr unsigned char kDecodingTable[] = {
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62,
      64, 64, 64, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64, 64, 0,
      1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
      23, 24, 25, 64, 64, 64, 64, 64, 64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38,
      39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64};

  size_t in_len = encoded_string.size();
  if (in_len % 4 != 0) throw std::runtime_error("Invalid base64 length");

  size_t out_len = in_len / 4 * 3;
  if (encoded_string[in_len - 1] == '=') out_len--;
  if (encoded_string[in_len - 2] == '=') out_len--;

  std::vector<unsigned char> out(out_len);
  for (size_t i = 0, j = 0; i < in_len;) {
    uint32_t a = encoded_string[i] == '='
                     ? 0 & i++
                     : kDecodingTable[static_cast<unsigned char>(encoded_string[i++])];
    uint32_t b = encoded_string[i] == '='
                     ? 0 & i++
                     : kDecodingTable[static_cast<unsigned char>(encoded_string[i++])];
    uint32_t c = encoded_string[i] == '='
                     ? 0 & i++
                     : kDecodingTable[static_cast<unsigned char>(encoded_string[i++])];
    uint32_t d = encoded_string[i] == '='
                     ? 0 & i++
                     : kDecodingTable[static_cast<unsigned char>(encoded_string[i++])];

    uint32_t triple = (a << 18) + (b << 12) + (c << 6) + d;

    if (j < out_len) out[j++] = (triple >> 16) & 0xFF;
    if (j < out_len) out[j++] = (triple >> 8) & 0xFF;
    if (j < out_len) out[j++] = triple & 0xFF;
  }

  return out;
}

std::shared_ptr<Image> LoadImage(const SVGIRI& href) {
  const auto& base64URL = href.iri();
  auto pos = base64URL.find("base64,");
  if (pos == std::string::npos) {
    return nullptr;
  }
  std::string base64Data = base64URL.substr(pos + 7);
  std::vector<unsigned char> imageData = base64_decode(base64Data);
  auto data = Data::MakeWithCopy(imageData.data(), imageData.size());

  return Image::MakeFromEncoded(data);
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