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
#include "core/ImageDecoder.h"
#include "core/utils/Log.h"
#include "fstream"
#include "gpu/ResourceProvider.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Rect.h"
#include "tgfx/svg/SVGAttributeParser.h"

namespace tgfx {

bool SkSVGImage::parseAndSetAttribute(const char* n, const char* v) {
  return INHERITED::parseAndSetAttribute(n, v) ||
         this->setX(SVGAttributeParser::parse<SVGLength>("x", n, v)) ||
         this->setY(SVGAttributeParser::parse<SVGLength>("y", n, v)) ||
         this->setWidth(SVGAttributeParser::parse<SVGLength>("width", n, v)) ||
         this->setHeight(SVGAttributeParser::parse<SVGLength>("height", n, v)) ||
         this->setHref(SVGAttributeParser::parse<SVGIRI>("xlink:href", n, v)) ||
         this->setPreserveAspectRatio(
             SVGAttributeParser::parse<SVGPreserveAspectRatio>("preserveAspectRatio", n, v));
}

#ifndef RENDER_SVG
bool SkSVGImage::onPrepareToRender(SVGRenderContext* ctx) const {
  // Width or height of 0 disables rendering per spec:
  // https://www.w3.org/TR/SVG11/struct.html#ImageElement
  return !fHref.iri().empty() && fWidth.value() > 0 && fHeight.value() > 0 &&
         INHERITED::onPrepareToRender(ctx);
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

std::shared_ptr<Image> LoadImage(const std::shared_ptr<ResourceProvider>&, const SVGIRI& href) {
  const auto& base64URL = href.iri();
  auto pos = base64URL.find("base64,");
  if (pos == std::string::npos) {
    return nullptr;
  }
  std::string base64Data = base64URL.substr(pos + 7);
  std::vector<unsigned char> imageData = base64_decode(base64Data);
  auto data = Data::MakeWithCopy(imageData.data(), imageData.size());
  // {
  //   std::ofstream out("/Users/yg/Downloads/yg2.png", std::ios::binary);
  //   if (!out) {
  //     return nullptr;
  //   }
  //   out.write(reinterpret_cast<const char*>(data->data()),
  //             static_cast<std::streamsize>(data->size()));
  // }
  return Image::MakeFromEncoded(data);

  // TODO: It may be better to use the SVG 'id' attribute as the asset id, to allow
  // clients to perform asset substitution based on element id.
  // sk_sp<skresources::ImageAsset> imageAsset;
  // switch (href.type()) {
  //   case SVGIRI::Type::kDataURI:
  //     imageAsset = rp.loadImageAsset("", href.iri().c_str(), "");
  //     break;
  //   case SVGIRI::Type::kNonlocal: {
  //     const auto path = SkOSPath::Dirname(href.iri().c_str());
  //     const auto name = SkOSPath::Basename(href.iri().c_str());
  //     imageAsset = rp->loadImageAsset(path.c_str(), name.c_str(), /* id */ name.c_str());
  //     break;
  //   }
  //   default:
  //     return nullptr;
  // }

  // return imageAsset ? imageAsset->getFrameData(0).image : nullptr;
}

SkSVGImage::ImageInfo SkSVGImage::LoadImage(const std::shared_ptr<ResourceProvider>& rp,
                                            const SVGIRI& iri, const Rect& viewPort,
                                            SVGPreserveAspectRatio /*par*/) {
  // TODO: svg sources
  std::shared_ptr<Image> image = ::tgfx::LoadImage(rp, iri);
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

void SkSVGImage::onRender(const SVGRenderContext& ctx) const {
  // Per spec: x, w, width, height attributes establish the new viewport.
  const SVGLengthContext& lctx = ctx.lengthContext();
  const Rect viewPort = lctx.resolveRect(fX, fY, fWidth, fHeight);

  //TODO (YG)
  ImageInfo image;
  const auto imgInfo = LoadImage(ctx.resourceProvider(), fHref, viewPort, fPreserveAspectRatio);
  if (!imgInfo.fImage) {
    LOGE("can't render image: load image failed\n");
    return;
  }

  // TODO: image-rendering property
  auto matrix = Matrix::MakeScale(viewPort.width() / imgInfo.fDst.width(),
                                  viewPort.height() / imgInfo.fDst.height());
  matrix.preTranslate(imgInfo.fDst.x(), imgInfo.fDst.y());

  ctx.canvas()->drawImage(imgInfo.fImage, matrix);

  // drawImageRect(imgInfo.fImage, imgInfo.fDst, SkSamplingOptions(SkFilterMode::kLinear));
}

Path SkSVGImage::onAsPath(const SVGRenderContext&) const {
  return {};
}

Rect SkSVGImage::onObjectBoundingBox(const SVGRenderContext& ctx) const {
  const SVGLengthContext& lctx = ctx.lengthContext();
  return lctx.resolveRect(fX, fY, fWidth, fHeight);
}
#endif
}  // namespace tgfx