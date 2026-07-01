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

#include "ElementWriter.h"
#include "SVGExportContext.h"
#include "SVGUtils.h"
#include "core/codecs/jpeg/JpegCodec.h"
#include "core/codecs/png/PngCodec.h"
#include "core/filters/AlphaThresholdColorFilter.h"
#include "core/filters/ComposeColorFilter.h"
#include "core/filters/ComposeImageFilter.h"
#include "core/filters/GaussianBlurImageFilter.h"
#include "core/filters/LumaColorFilter.h"
#include "core/filters/ShaderMaskFilter.h"
#include "core/shaders/ColorFilterShader.h"
#include "core/shaders/MatrixShader.h"
#include "core/shaders/PerlinNoiseShader.h"
#include "core/utils/ColorHelper.h"
#include "core/utils/ColorSpaceHelper.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "core/utils/Types.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/GradientType.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Pixmap.h"
#include "tgfx/core/RRect.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/svg/SVGPathParser.h"

namespace tgfx {

Resources::Resources(const Color& color) {
  colorValue = color;
  paintColor = ToSVGColor(color);
}

ElementWriter::ElementWriter(const std::string& name, XMLWriter* writer,
                             std::shared_ptr<ColorSpace> targetColorSpace,
                             std::shared_ptr<ColorSpace> assignColorSpace)
    : writer(writer), _targetColorSpace(std::move(targetColorSpace)),
      _assignColorSpace(std::move(assignColorSpace)) {
  writer->startElement(name);
}

ElementWriter::ElementWriter(const std::string& name, const std::unique_ptr<XMLWriter>& writer,
                             std::shared_ptr<ColorSpace> targetColorSpace,
                             std::shared_ptr<ColorSpace> assignColorSpace)
    : ElementWriter(name, writer.get(), std::move(targetColorSpace), std::move(assignColorSpace)) {
}

ElementWriter::ElementWriter(const std::string& name, const std::unique_ptr<XMLWriter>& writer,
                             ResourceStore* bucket, std::shared_ptr<ColorSpace> targetColorSpace,
                             std::shared_ptr<ColorSpace> assignColorSpace)
    : writer(writer.get()), resourceStore(bucket), _targetColorSpace(std::move(targetColorSpace)),
      _assignColorSpace(std::move(assignColorSpace)) {
  writer->startElement(name);
}

ElementWriter::ElementWriter(const std::string& name, Context* context,
                             SVGExportContext* svgContext, XMLWriter* writer, ResourceStore* bucket,
                             bool disableWarning, const Matrix& matrix, const Brush& brush,
                             const Stroke* stroke, std::shared_ptr<ColorSpace> targetColorSpace,
                             std::shared_ptr<ColorSpace> assignColorSpace)
    : writer(writer), resourceStore(bucket), disableWarning(disableWarning),
      _targetColorSpace(std::move(targetColorSpace)),
      _assignColorSpace(std::move(assignColorSpace)) {
  generateWriteColorSpaceString();
  Resources resource = addResources(brush, context, svgContext);

  writer->startElement(name);

  addFillAndStroke(brush, stroke, resource);

  if (!matrix.isIdentity()) {
    addAttribute("transform", ToSVGTransform(matrix));
  }
}

ElementWriter::~ElementWriter() {
  writer->endElement();
  resourceStore = nullptr;
}

void ElementWriter::reportUnsupportedElement(const char* message) const {
  if (!disableWarning) {
    LOGE("[SVG exporting]:%s", message);
  }
}

bool ElementWriter::writeColorCSSStyleAttribute(const std::string& attributeName, Color color,
                                                std::string* retString) {
  if (_writeColorSpaceString.empty()) {
    return false;
  }
  *retString += attributeName + ":" + ToSVGColor(color) + ";" + attributeName + ":color(" +
                _writeColorSpaceString + " " + FloatToString(color.red) + " " +
                FloatToString(color.green) + " " + FloatToString(color.blue) + ");";
  return true;
}

void ElementWriter::generateWriteColorSpaceString() {
  static std::shared_ptr<ColorSpace> srgb = ColorSpace::SRGB();
  static std::shared_ptr<ColorSpace> displayP3 = ColorSpace::DisplayP3();
  static std::shared_ptr<ColorSpace> a98rgb =
      ColorSpace::MakeRGB(NamedTransferFunction::A98RGB, NamedGamut::AdobeRGB);
  static std::shared_ptr<ColorSpace> rec2020 =
      ColorSpace::MakeRGB(NamedTransferFunction::Rec2020, NamedGamut::Rec2020);
  ColorMatrix33 matrix{};
  NamedPrimaries::ProPhotoRGB.toXYZD50(&matrix);
  static std::shared_ptr<ColorSpace> prophoto =
      ColorSpace::MakeRGB(NamedTransferFunction::ProPhotoRGB, matrix);
  auto tempColorSpace = _assignColorSpace ? _assignColorSpace : _targetColorSpace;
  if (ColorSpace::Equals(tempColorSpace.get(), displayP3.get())) {
    _writeColorSpaceString = "display-p3";
  } else if (ColorSpace::Equals(tempColorSpace.get(), a98rgb.get())) {
    _writeColorSpaceString = "a98-rgb";
  } else if (ColorSpace::Equals(tempColorSpace.get(), rec2020.get())) {
    _writeColorSpaceString = "rec2020";
  } else {
    _writeColorSpaceString = std::string{};
  }
}

void ElementWriter::addFillAndStroke(const Brush& brush, const Stroke* stroke,
                                     const Resources& resources) {
  if (!stroke) {  //fill draw
    static const std::string defaultFill = "black";
    if (resources.paintColor != defaultFill) {
      addAttribute("fill", resources.paintColor);
    }
    if (!brush.isOpaque()) {
      addAttribute("fill-opacity", brush.color.alpha);
    }
  } else {  //stroke draw
    addAttribute("fill", "none");

    addAttribute("stroke", resources.paintColor);

    float strokeWidth = stroke->width;
    if (strokeWidth == 0) {
      // Hairline stroke
      strokeWidth = 1;
      addAttribute("vector-effect", "non-scaling-stroke");
    }
    addAttribute("stroke-width", strokeWidth);

    auto cap = ToSVGCap(stroke->cap);
    if (!cap.empty()) {
      addAttribute("stroke-linecap", cap);
    }

    auto join = ToSVGJoin(stroke->join);
    if (!join.empty()) {
      addAttribute("stroke-linejoin", join);
    }

    if (stroke->join == LineJoin::Miter && !FloatNearlyEqual(stroke->miterLimit, 4.f)) {
      addAttribute("stroke-miterlimit", stroke->miterLimit);
    }

    if (!brush.isOpaque()) {
      addAttribute("stroke-opacity", brush.color.alpha);
    }
  }
  std::string cssStyle;
  if (resources.paintColor.find("url") == std::string::npos) {
    writeColorCSSStyleAttribute(stroke ? "stroke" : "fill", resources.colorValue, &cssStyle);
  }

  if (brush.blendMode != BlendMode::SrcOver) {
    auto blendModeString = ToSVGBlendMode(brush.blendMode);
    if (!blendModeString.empty()) {
      blendModeString = "mix-blend-mode:" + blendModeString;
      cssStyle += blendModeString + ";";
    } else {
      reportUnsupportedElement("Unsupported blend mode");
    }
  }
  if (!cssStyle.empty()) {
    addAttribute("style", cssStyle);
  }

  if (!resources.filter.empty()) {
    addAttribute("filter", resources.filter);
  }

  if (!resources.mask.empty()) {
    addAttribute("mask", resources.mask);
  }
}

void ElementWriter::addAttribute(const std::string& name, const std::string& val) {
  writer->addAttribute(name, val);
}

void ElementWriter::addAttribute(const std::string& name, int32_t val) {
  writer->addS32Attribute(name, val);
}

void ElementWriter::addAttribute(const std::string& name, float val) {
  writer->addScalarAttribute(name, val);
}

void ElementWriter::addText(const std::string& text) {
  writer->addText(text);
}

void ElementWriter::addFontAttributes(const Font& font) {
  addAttribute("font-size", font.getSize());

  auto typeface = font.getTypeface();
  if (typeface == nullptr) {
    return;
  }
  auto familyName = typeface->fontFamily();
  if (!familyName.empty()) {
    addAttribute("font-family", familyName);
  }

  if (font.isFauxItalic()) {
    addAttribute("font-style", "italic");
  }
  if (font.isFauxBold()) {
    addAttribute("font-weight", "bold");
  }
}

void ElementWriter::addRectAttributes(const Rect& rect) {
  // x, y default to 0
  if (rect.x() != 0) {
    addAttribute("x", rect.x());
  }
  if (rect.y() != 0) {
    addAttribute("y", rect.y());
  }

  addAttribute("width", rect.width());
  addAttribute("height", rect.height());
}

void ElementWriter::addRoundRectAttributes(const RRect& roundRect) {
  // SVG <rect> only supports uniform rx/ry; complex RRects must be emitted as <path>.
  DEBUG_ASSERT(!roundRect.isComplex());
  addRectAttributes(roundRect.rect());
  if (FloatNearlyZero(roundRect.radii()[0].x) && FloatNearlyZero(roundRect.radii()[0].y)) {
    return;
  }
  addAttribute("rx", roundRect.radii()[0].x);
  addAttribute("ry", roundRect.radii()[0].y);
}

void ElementWriter::addCircleAttributes(const Rect& bound) {
  addAttribute("cx", bound.centerX());
  addAttribute("cy", bound.centerY());
  addAttribute("r", bound.width() * 0.5f);
}

void ElementWriter::addEllipseAttributes(const Rect& bound) {
  addAttribute("cx", bound.centerX());
  addAttribute("cy", bound.centerY());
  addAttribute("rx", bound.width() * 0.5f);
  addAttribute("ry", bound.height() * 0.5f);
}

void ElementWriter::addPathAttributes(const Path& path, SVGPathParser::PathEncoding encoding) {
  addAttribute("d", SVGPathParser::ToSVGString(path, encoding));
}

Resources ElementWriter::addColorFilterResource(const std::shared_ptr<ColorFilter>& colorFilter) {
  Resources resources;
  if (!colorFilter) {
    return resources;
  }
  auto type = Types::Get(colorFilter.get());
  if (type == Types::ColorFilterType::Blend || type == Types::ColorFilterType::Matrix ||
      type == Types::ColorFilterType::Compose || type == Types::ColorFilterType::Luma ||
      type == Types::ColorFilterType::AlphaThreshold) {
    std::string filterID = resourceStore->addFilter();
    {
      ElementWriter filterElement("filter", writer);
      filterElement.addAttribute("id", filterID);
      filterElement.addAttribute("color-interpolation-filters", "sRGB");
      filterElement.addAttribute("x", "0%");
      filterElement.addAttribute("y", "0%");
      filterElement.addAttribute("width", "100%");
      filterElement.addAttribute("height", "100%");
      addColorFilterPrimitives(colorFilter);
    }
    resources.filter = "url(#" + filterID + ")";
  }
  return resources;
}

bool ElementWriter::writeFilterPrimitives(const std::shared_ptr<ImageFilter>& imageFilter,
                                          ElementWriter& filterElement,
                                          const std::shared_ptr<SVGCustomWriter>& exportWriter,
                                          const Rect& bound, Context* context,
                                          bool preserveSoftAlpha) {
  auto type = Types::Get(imageFilter.get());
  switch (type) {
    case Types::ImageFilterType::Blur: {
      const auto blurFilter = static_cast<const GaussianBlurImageFilter*>(imageFilter.get());
      callbackBlurImageFilter(blurFilter, exportWriter, filterElement);
      addBlurImageFilter(blurFilter);
      break;
    }
    case Types::ImageFilterType::DropShadow: {
      const auto dropShadowFilter = static_cast<const DropShadowImageFilter*>(imageFilter.get());
      callbackDropShadowImageFilter(dropShadowFilter, exportWriter, filterElement);
      addDropShadowImageFilter(dropShadowFilter, "", preserveSoftAlpha);
      break;
    }
    case Types::ImageFilterType::InnerShadow: {
      const auto innerShadowFilter = static_cast<const InnerShadowImageFilter*>(imageFilter.get());
      callbackInnerShadowImageFilter(innerShadowFilter, exportWriter, filterElement);
      addInnerShadowImageFilter(innerShadowFilter, "", preserveSoftAlpha);
      break;
    }
    case Types::ImageFilterType::Color: {
      const auto colorFilter = static_cast<const ColorImageFilter*>(imageFilter.get());
      addColorImageFilter(colorFilter);
      break;
    }
    case Types::ImageFilterType::Blend: {
      const auto blendFilter = static_cast<const BlendImageFilter*>(imageFilter.get());
      addBlendImageFilter(blendFilter, "", &bound, context);
      break;
    }
    default:
      reportUnsupportedElement("Unsupported image filter");
      return false;
  }
  return true;
}

std::string ElementWriter::emitFilterElement(const std::shared_ptr<ImageFilter>& imageFilter,
                                             const Rect& bound,
                                             const std::shared_ptr<SVGCustomWriter>& exportWriter,
                                             Context* context, bool preserveSoftAlpha) {
  std::string filterID = resourceStore->addFilter();
  ElementWriter filterElement("filter", writer);
  filterElement.addAttribute("id", filterID);
  filterElement.addAttribute("color-interpolation-filters", "sRGB");
  auto type = Types::Get(imageFilter.get());
  // InnerShadow needs extra space for the shadow offset since filterBounds() does not expand.
  float extraWidth = 0;
  float extraHeight = 0;
  if (type == Types::ImageFilterType::InnerShadow) {
    const auto innerShadowFilter = static_cast<const InnerShadowImageFilter*>(imageFilter.get());
    extraWidth = innerShadowFilter->dx;
    extraHeight = innerShadowFilter->dy;
  }
  filterElement.addAttribute("x", bound.x());
  filterElement.addAttribute("y", bound.y());
  filterElement.addAttribute("width", bound.width() + extraWidth);
  filterElement.addAttribute("height", bound.height() + extraHeight);
  filterElement.addAttribute("filterUnits", "userSpaceOnUse");
  if (!writeFilterPrimitives(imageFilter, filterElement, exportWriter, bound, context,
                             preserveSoftAlpha)) {
    return "";
  }
  return filterID;
}

std::string ElementWriter::addImageFilter(const std::shared_ptr<ImageFilter>& imageFilter,
                                          const Rect& bound,
                                          const std::shared_ptr<SVGCustomWriter>& exportWriter,
                                          Context* context) {
  auto filteredBound = imageFilter->filterBounds(bound);
  return emitFilterElement(imageFilter, filteredBound, exportWriter, context, false);
}

std::vector<std::string> ElementWriter::addImageFilterChain(
    const std::shared_ptr<ImageFilter>& imageFilter, const Rect& bound,
    const std::shared_ptr<SVGCustomWriter>& exportWriter, Context* context) {
  if (!imageFilter) {
    return {};
  }
  auto type = Types::Get(imageFilter.get());
  if (type == Types::ImageFilterType::Compose) {
    const auto composeFilter = static_cast<const ComposeImageFilter*>(imageFilter.get());
    // GPU pipeline: each sub-filter receives the previous sub-filter's full output as its source.
    // In SVG, nested <g filter="..."> achieves exactly this: each layer's SourceGraphic/SourceAlpha
    // is the output of the inner layer's filter. This matches the GPU's serial FilterImage chain.
    auto composeBound = imageFilter->filterBounds(bound);
    std::vector<std::string> filterIDs;
    for (const auto& filterItem : composeFilter->filters) {
      filterIDs.push_back(emitFilterElement(filterItem, composeBound, exportWriter, context, true));
    }
    return filterIDs;
  }
  auto id = addImageFilter(imageFilter, bound, exportWriter, context);
  if (!id.empty()) {
    return {id};
  }
  return {};
}

void ElementWriter::callbackBlurImageFilter(const GaussianBlurImageFilter* filter,
                                            const std::shared_ptr<SVGCustomWriter>& exportWriter,
                                            ElementWriter& filterElement) {
  if (!exportWriter) {
    return;
  }
  auto attribute = exportWriter->writeBlurImageFilter(filter->blurrinessX, filter->blurrinessY,
                                                      filter->tileMode);
  if (!attribute.name.empty() && !attribute.value.empty()) {
    filterElement.addAttribute(attribute.name, attribute.value);
  }
}

void ElementWriter::callbackDropShadowImageFilter(
    const DropShadowImageFilter* filter, const std::shared_ptr<SVGCustomWriter>& exportWriter,
    ElementWriter& filterElement) {
  if (!exportWriter) {
    return;
  }
  float blurrinessX = 0.f;
  float blurrinessY = 0.f;
  if (filter->blurFilter) {
    if (Types::Get(filter->blurFilter.get()) == Types::ImageFilterType::Blur) {
      const auto blurFilter = static_cast<const GaussianBlurImageFilter*>(filter->blurFilter.get());
      blurrinessX = blurFilter->blurrinessX;
      blurrinessY = blurFilter->blurrinessY;
    }
  }
  auto attribute = exportWriter->writeDropShadowImageFilter(
      filter->dx, filter->dy, blurrinessX, blurrinessY, filter->color, filter->shadowOnly);
  if (!attribute.name.empty() && !attribute.value.empty()) {
    filterElement.addAttribute(attribute.name, attribute.value);
  }
}

void ElementWriter::callbackInnerShadowImageFilter(
    const InnerShadowImageFilter* filter, const std::shared_ptr<SVGCustomWriter>& exportWriter,
    ElementWriter& filterElement) {
  if (!exportWriter) {
    return;
  }
  float blurrinessX = 0.f;
  float blurrinessY = 0.f;
  if (filter->blurFilter) {
    if (Types::Get(filter->blurFilter.get()) == Types::ImageFilterType::Blur) {
      const auto blurFilter = static_cast<const GaussianBlurImageFilter*>(filter->blurFilter.get());
      blurrinessX = blurFilter->blurrinessX;
      blurrinessY = blurFilter->blurrinessY;
    }
  }
  auto attribute = exportWriter->writeInnerShadowImageFilter(
      filter->dx, filter->dy, blurrinessX, blurrinessY, filter->color, filter->shadowOnly);
  if (!attribute.name.empty() && !attribute.value.empty()) {
    filterElement.addAttribute(attribute.name, attribute.value);
  }
}

void ElementWriter::addBlurImageFilter(const GaussianBlurImageFilter* filter,
                                       const std::string& inputResult) {
  ElementWriter blurElement("feGaussianBlur", writer);
  if (!inputResult.empty()) {
    blurElement.addAttribute("in", inputResult);
  }
  if (FloatNearlyEqual(filter->blurrinessX, filter->blurrinessY)) {
    blurElement.addAttribute("stdDeviation", filter->blurrinessX);
  } else {
    blurElement.addAttribute("stdDeviation", FloatToString(filter->blurrinessX) + " " +
                                                 FloatToString(filter->blurrinessY));
  }
  blurElement.addAttribute("result", "blur");
}

void ElementWriter::addHardAlphaElement() {
  ElementWriter colorMatrixElement("feColorMatrix", writer);
  colorMatrixElement.addAttribute("in", "SourceAlpha");
  colorMatrixElement.addAttribute("type", "matrix");
  colorMatrixElement.addAttribute("values", "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 127 0");
  colorMatrixElement.addAttribute("result", "hardAlpha");
}

void ElementWriter::addSoftAlphaElement() {
  ElementWriter colorMatrixElement("feColorMatrix", writer);
  colorMatrixElement.addAttribute("in", "SourceAlpha");
  colorMatrixElement.addAttribute("type", "matrix");
  // Match the GPU pipeline: preserve the source's soft alpha instead of hardening blurred edges.
  colorMatrixElement.addAttribute("values", "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0");
  colorMatrixElement.addAttribute("result", "softAlpha");
}

void ElementWriter::addDropShadowImageFilter(const DropShadowImageFilter* filter,
                                             const std::string& inputResult,
                                             bool preserveSoftAlpha) {
  const auto alphaResult = preserveSoftAlpha ? "softAlpha" : "hardAlpha";
  if (preserveSoftAlpha) {
    addSoftAlphaElement();
  } else {
    addHardAlphaElement();
  }
  {
    ElementWriter offsetElement("feOffset", writer);
    offsetElement.addAttribute("dx", filter->dx);
    offsetElement.addAttribute("dy", filter->dy);
  }
  {
    ElementWriter blurElement("feGaussianBlur", writer);
    float blurX = 0.f;
    float blurY = 0.f;
    if (filter->blurFilter &&
        Types::Get(filter->blurFilter.get()) == Types::ImageFilterType::Blur) {
      const auto blurFilter = static_cast<const GaussianBlurImageFilter*>(filter->blurFilter.get());
      blurX = blurFilter->blurrinessX;
      blurY = blurFilter->blurrinessY;
    }
    if (FloatNearlyEqual(blurX, blurY)) {
      blurElement.addAttribute("stdDeviation", blurX);
    } else {
      blurElement.addAttribute("stdDeviation", FloatToString(blurX) + " " + FloatToString(blurY));
    }
  }
  if (!filter->shadowOnly) {
    ElementWriter compositeElement("feComposite", writer);
    compositeElement.addAttribute("in2", alphaResult);
    compositeElement.addAttribute("operator", "out");
  }
  {
    ElementWriter colorMatrixElement("feColorMatrix", writer);
    colorMatrixElement.addAttribute("type", "matrix");
    auto color = filter->color;
    color = ConvertColorSpace(color, _targetColorSpace);
    colorMatrixElement.addAttribute("values", "0 0 0 0 " + FloatToString(color.red) + " 0 0 0 0 " +
                                                  FloatToString(color.green) + " 0 0 0 0 " +
                                                  FloatToString(color.blue) + " 0 0 0 " +
                                                  FloatToString(color.alpha) + " 0");
    colorMatrixElement.addAttribute("result", "shadow");
  }
  if (!filter->shadowOnly) {
    auto sourceRef = inputResult.empty() ? "SourceGraphic" : inputResult;
    ElementWriter blendElement("feBlend", writer);
    blendElement.addAttribute("mode", "normal");
    blendElement.addAttribute("in", sourceRef);
    blendElement.addAttribute("in2", "shadow");
  }
}
void ElementWriter::addInnerShadowImageFilter(const InnerShadowImageFilter* filter,
                                              const std::string& inputResult,
                                              bool preserveSoftAlpha) {
  if (!filter->blurFilter) {
    return;
  }
  const auto alphaResult = preserveSoftAlpha ? "softAlpha" : "hardAlpha";
  if (preserveSoftAlpha) {
    addSoftAlphaElement();
  } else {
    addHardAlphaElement();
  }
  auto sourceRef = inputResult.empty() ? std::string("SourceGraphic") : inputResult;
  if (!filter->shadowOnly) {
    {
      ElementWriter floodElement("feFlood", writer);
      floodElement.addAttribute("flood-opacity", "0");
      floodElement.addAttribute("result", "BackgroundImageFix");
    }
    {
      ElementWriter blendElement("feBlend", writer);
      blendElement.addAttribute("mode", "normal");
      blendElement.addAttribute("in", sourceRef);
      blendElement.addAttribute("in2", "BackgroundImageFix");
      blendElement.addAttribute("result", "shape");
    }
  }
  {
    ElementWriter offsetElement("feOffset", writer);
    offsetElement.addAttribute("in", alphaResult);
    offsetElement.addAttribute("dx", filter->dx);
    offsetElement.addAttribute("dy", filter->dy);
    offsetElement.addAttribute("result", "offsetAlpha");
  }
  {
    ElementWriter blurElement("feGaussianBlur", writer);
    blurElement.addAttribute("in", "offsetAlpha");
    if (Types::Get(filter->blurFilter.get()) == Types::ImageFilterType::Blur) {
      auto blurFilter = static_cast<const GaussianBlurImageFilter*>(filter->blurFilter.get());
      if (FloatNearlyEqual(blurFilter->blurrinessX, blurFilter->blurrinessY)) {
        blurElement.addAttribute("stdDeviation", blurFilter->blurrinessX);
      } else {
        blurElement.addAttribute("stdDeviation", FloatToString(blurFilter->blurrinessX) + " " +
                                                     FloatToString(blurFilter->blurrinessY));
      }
    }
    blurElement.addAttribute("result", "blurredAlpha");
  }
  if (!preserveSoftAlpha) {
    {
      ElementWriter compositeElement("feComposite", writer);
      compositeElement.addAttribute("in2", alphaResult);
      compositeElement.addAttribute("operator", "arithmetic");
      compositeElement.addAttribute("k2", "-1");
      compositeElement.addAttribute("k3", "1");
    }
    {
      ElementWriter colorMatrixElement("feColorMatrix", writer);
      colorMatrixElement.addAttribute("type", "matrix");
      auto color = ConvertColorSpace(filter->color, _targetColorSpace);
      colorMatrixElement.addAttribute("values", "0 0 0 0 " + FloatToString(color.red) +
                                                    " 0 0 0 0 " + FloatToString(color.green) +
                                                    " 0 0 0 0 " + FloatToString(color.blue) +
                                                    " 0 0 0 " + FloatToString(color.alpha) + " 0");
    }
    if (!filter->shadowOnly) {
      ElementWriter blendElement("feBlend", writer);
      blendElement.addAttribute("mode", "normal");
      blendElement.addAttribute("in2", "shape");
    }
    return;
  }

  auto color = ConvertColorSpace(filter->color, _targetColorSpace);
  {
    ElementWriter floodElement("feFlood", writer);
    floodElement.addAttribute("flood-color", ToSVGColor(color));
    floodElement.addAttribute("flood-opacity", color.alpha);
    floodElement.addAttribute("result", "shadowColor");
    std::string cssStyle;
    if (writeColorCSSStyleAttribute("flood-color", color, &cssStyle)) {
      floodElement.addAttribute("style", cssStyle);
    }
  }
  {
    // Match InnerShadowImageFilter::getShadowFragmentProcessor():
    // colorShadow = SrcOut(shadowColor, blurredAlpha).
    ElementWriter compositeElement("feComposite", writer);
    compositeElement.addAttribute("in", "shadowColor");
    compositeElement.addAttribute("in2", "blurredAlpha");
    compositeElement.addAttribute("operator", "out");
    compositeElement.addAttribute("result", "colorShadow");
  }
  if (filter->shadowOnly) {
    ElementWriter compositeElement("feComposite", writer);
    compositeElement.addAttribute("in", "colorShadow");
    compositeElement.addAttribute("in2", "shape");
    compositeElement.addAttribute("operator", "in");
    return;
  }
  {
    // Match InnerShadowImageFilter::asFragmentProcessor():
    // result = SrcATop(colorShadow, source).
    ElementWriter shadowInsideElement("feComposite", writer);
    shadowInsideElement.addAttribute("in", "colorShadow");
    shadowInsideElement.addAttribute("in2", "shape");
    shadowInsideElement.addAttribute("operator", "in");
    shadowInsideElement.addAttribute("result", "shadowInside");
  }
  {
    ElementWriter shapeOutsideElement("feComposite", writer);
    shapeOutsideElement.addAttribute("in", "shape");
    shapeOutsideElement.addAttribute("in2", "colorShadow");
    shapeOutsideElement.addAttribute("operator", "out");
    shapeOutsideElement.addAttribute("result", "shapeOutsideShadow");
  }
  {
    ElementWriter mergeElement("feMerge", writer);
    mergeElement.addAttribute("result", "innerShadowResult");
    {
      ElementWriter mergeNode("feMergeNode", writer);
      mergeNode.addAttribute("in", "shapeOutsideShadow");
    }
    {
      ElementWriter mergeNode("feMergeNode", writer);
      mergeNode.addAttribute("in", "shadowInside");
    }
  }
}

void ElementWriter::addColorImageFilter(const ColorImageFilter* filter,
                                        const std::string& inputResult) {
  if (!filter->filter) {
    return;
  }
  if (!addColorFilterPrimitives(filter->filter, inputResult)) {
    reportUnsupportedElement("Unsupported color filter in ColorImageFilter");
  }
}

std::string ElementWriter::emitShaderAsPrimitive(const Shader* shader, const Matrix& shaderMatrix,
                                                 const Rect* filterBounds, Context* context) {
  auto resultName = resourceStore->addFilterResult();
  switch (Types::Get(shader)) {
    case Types::ShaderType::Color: {
      const auto colorShader = static_cast<const ColorShader*>(shader);
      Color color;
      if (!colorShader->asColor(&color)) {
        return "";
      }
      color = ConvertColorSpace(color, _targetColorSpace);
      ElementWriter floodElement("feFlood", writer);
      floodElement.addAttribute("flood-color", ToSVGColor(color));
      if (!color.isOpaque()) {
        floodElement.addAttribute("flood-opacity", color.alpha);
      }
      floodElement.addAttribute("result", resultName);
      std::string cssStyle;
      if (writeColorCSSStyleAttribute("flood-color", color, &cssStyle)) {
        floodElement.addAttribute("style", cssStyle);
      }
      return resultName;
    }
    case Types::ShaderType::PerlinNoise: {
      addPerlinNoisePrimitives(shader, resultName);
      return resultName;
    }
    case Types::ShaderType::Gradient: {
      auto gradientShader = static_cast<const GradientShader*>(shader);
      GradientInfo info;
      GradientType gradientType = gradientShader->asGradient(&info);
      std::string svgFragment = "<svg xmlns='http://www.w3.org/2000/svg'";
      if (filterBounds) {
        svgFragment += " width='" + FloatToString(filterBounds->width()) + "'";
        svgFragment += " height='" + FloatToString(filterBounds->height()) + "'";
        svgFragment += " viewBox='" + FloatToString(filterBounds->x()) + " " +
                       FloatToString(filterBounds->y()) + " " +
                       FloatToString(filterBounds->width()) + " " +
                       FloatToString(filterBounds->height()) + "'";
      }
      svgFragment += ">";
      // Shader gradient coordinates are in local space (relative to the shape's top-left).
      // Offset them by filterBounds origin to convert to the viewBox coordinate system.
      float offsetX = filterBounds ? filterBounds->x() : 0;
      float offsetY = filterBounds ? filterBounds->y() : 0;
      if (gradientType == GradientType::Linear) {
        svgFragment += "<defs><linearGradient id='g' gradientUnits='userSpaceOnUse'";
        svgFragment += " x1='" + FloatToString(info.points[0].x + offsetX) + "'";
        svgFragment += " y1='" + FloatToString(info.points[0].y + offsetY) + "'";
        svgFragment += " x2='" + FloatToString(info.points[1].x + offsetX) + "'";
        svgFragment += " y2='" + FloatToString(info.points[1].y + offsetY) + "'>";
      } else if (gradientType == GradientType::Radial) {
        svgFragment += "<defs><radialGradient id='g' gradientUnits='userSpaceOnUse'";
        svgFragment += " cx='" + FloatToString(info.points[0].x + offsetX) + "'";
        svgFragment += " cy='" + FloatToString(info.points[0].y + offsetY) + "'";
        svgFragment += " r='" + FloatToString(info.radiuses[0]) + "'>";
      } else {
        reportUnsupportedElement("Unsupported gradient type in emitShaderAsPrimitive");
        return "";
      }
      for (size_t i = 0; i < info.colors.size(); ++i) {
        auto color = ConvertColorSpace(info.colors[i], _targetColorSpace);
        svgFragment += "<stop offset='" + FloatToString(info.positions[i]) + "'";
        svgFragment += " stop-color='" + ToSVGColor(color) + "'";
        if (!color.isOpaque()) {
          svgFragment += " stop-opacity='" + FloatToString(color.alpha) + "'";
        }
        svgFragment += "/>";
      }
      if (gradientType == GradientType::Linear) {
        svgFragment += "</linearGradient></defs>";
      } else {
        svgFragment += "</radialGradient></defs>";
      }
      svgFragment += "<rect fill='url(#g)'";
      if (filterBounds) {
        svgFragment += " x='" + FloatToString(filterBounds->x()) + "'";
        svgFragment += " y='" + FloatToString(filterBounds->y()) + "'";
        svgFragment += " width='" + FloatToString(filterBounds->width()) + "'";
        svgFragment += " height='" + FloatToString(filterBounds->height()) + "'";
      }
      svgFragment += "/></svg>";
      auto svgData = reinterpret_cast<const unsigned char*>(svgFragment.data());
      auto svgLength = svgFragment.size();
      auto base64Length = ((svgLength + 2) / 3) * 4;
      std::string base64String(base64Length, '\0');
      Base64Encode(svgData, svgLength, base64String.data());
      ElementWriter feImageElement("feImage", writer);
      feImageElement.addAttribute("xlink:href", "data:image/svg+xml;base64," + base64String);
      if (filterBounds) {
        feImageElement.addAttribute("x", filterBounds->x());
        feImageElement.addAttribute("y", filterBounds->y());
        feImageElement.addAttribute("width", filterBounds->width());
        feImageElement.addAttribute("height", filterBounds->height());
      }
      feImageElement.addAttribute("result", resultName);
      return resultName;
    }
    case Types::ShaderType::Image: {
      const auto imageShader = static_cast<const ImageShader*>(shader);
      auto dataUri = SVGExportContext::EncodeImageToDataUri(imageShader->image, context);
      if (!dataUri) {
        return "";
      }
      ElementWriter imageElement("feImage", writer);
      auto imgRect = Rect::MakeWH(static_cast<float>(imageShader->image->width()),
                                  static_cast<float>(imageShader->image->height()));
      auto mappedRect = shaderMatrix.mapRect(imgRect);
      float offsetX = filterBounds ? filterBounds->x() : 0;
      float offsetY = filterBounds ? filterBounds->y() : 0;
      imageElement.addAttribute("x", mappedRect.x() + offsetX);
      imageElement.addAttribute("y", mappedRect.y() + offsetY);
      imageElement.addAttribute("width", mappedRect.width());
      imageElement.addAttribute("height", mappedRect.height());
      imageElement.addAttribute("preserveAspectRatio", "none");
      imageElement.addAttribute("xlink:href", static_cast<const char*>(dataUri->data()));
      imageElement.addAttribute("result", resultName);
      return resultName;
    }
    default:
      return "";
  }
}

void ElementWriter::addBlendImageFilter(const BlendImageFilter* filter,
                                        const std::string& inputResult, const Rect* filterBounds,
                                        Context* context) {
  if (!filter->shader) {
    reportUnsupportedElement("Missing shader in BlendImageFilter");
    return;
  }
  auto sourceRef = inputResult.empty() ? std::string("SourceGraphic") : inputResult;
  if (filter->blendMode == BlendMode::DstIn) {
    if (Types::Get(filter->shader.get()) != Types::ShaderType::Image) {
      reportUnsupportedElement("Unsupported DstIn shader type in BlendImageFilter");
      return;
    }
    const auto imageShader = static_cast<const ImageShader*>(filter->shader.get());
    auto dataUri = SVGExportContext::EncodeImageToDataUri(imageShader->image, context);
    if (!dataUri) {
      reportUnsupportedElement("Failed to encode DstIn image shader in BlendImageFilter");
      return;
    }
    {
      ElementWriter imageElement("feImage", writer);
      if (filterBounds) {
        imageElement.addAttribute("x", filterBounds->x());
        imageElement.addAttribute("y", filterBounds->y());
      }
      imageElement.addAttribute("width", imageShader->image->width());
      imageElement.addAttribute("height", imageShader->image->height());
      imageElement.addAttribute("xlink:href", static_cast<const char*>(dataUri->data()));
      imageElement.addAttribute("result", "blendMask");
    }
    {
      ElementWriter compositeElement("feComposite", writer);
      compositeElement.addAttribute("in", sourceRef);
      compositeElement.addAttribute("in2", "blendMask");
      compositeElement.addAttribute("operator", "in");
    }
    return;
  }
  auto blendModeString = ToSVGBlendMode(filter->blendMode);
  if (blendModeString.empty()) {
    reportUnsupportedElement("Unsupported blend mode in BlendImageFilter");
    return;
  }
  // Unwrap MatrixShader to get the actual shader type and accumulated matrix.
  const Shader* blendShader = filter->shader.get();
  Matrix shaderMatrix = Matrix::I();
  while (blendShader && Types::Get(blendShader) == Types::ShaderType::Matrix) {
    auto matrixShader = static_cast<const MatrixShader*>(blendShader);
    shaderMatrix = shaderMatrix * matrixShader->matrix;
    blendShader = matrixShader->source.get();
  }
  auto shaderType = Types::Get(blendShader);
  if (shaderType == Types::ShaderType::ColorFilter) {
    auto colorFilterShader = static_cast<const ColorFilterShader*>(blendShader);
    // Unwrap the inner shader's MatrixShader layer.
    const Shader* innerShader = colorFilterShader->shader.get();
    Matrix innerMatrix = shaderMatrix;
    while (innerShader && Types::Get(innerShader) == Types::ShaderType::Matrix) {
      auto ms = static_cast<const MatrixShader*>(innerShader);
      innerMatrix = innerMatrix * ms->matrix;
      innerShader = ms->source.get();
    }
    // Step 1: Emit inner shader as a filter primitive.
    auto shaderResult = emitShaderAsPrimitive(innerShader, innerMatrix, filterBounds, context);
    if (shaderResult.empty()) {
      reportUnsupportedElement("Unsupported inner shader in ColorFilterShader BlendImageFilter");
      return;
    }
    // Step 2: Apply the color filter on top of the inner shader output.
    std::string finalResult = shaderResult;
    if (colorFilterShader->colorFilter) {
      auto cfResult = resourceStore->addFilterResult();
      if (!addColorFilterPrimitives(colorFilterShader->colorFilter, shaderResult, cfResult)) {
        reportUnsupportedElement("Unsupported color filter in ColorFilterShader BlendImageFilter");
        return;
      }
      finalResult = cfResult;
    }
    // Step 3: Blend the processed result with the source.
    ElementWriter blendElement("feBlend", writer);
    blendElement.addAttribute("in", finalResult);
    blendElement.addAttribute("in2", sourceRef);
    blendElement.addAttribute("mode", blendModeString);
    return;
  }
  // Color, PerlinNoise, Gradient, Image shaders share the same pattern:
  // emit shader as filter primitive, then blend with source.
  auto resultName = emitShaderAsPrimitive(blendShader, shaderMatrix, filterBounds, context);
  if (resultName.empty()) {
    reportUnsupportedElement("Unsupported shader type in BlendImageFilter");
    return;
  }
  ElementWriter blendElement("feBlend", writer);
  blendElement.addAttribute("in", resultName);
  blendElement.addAttribute("in2", sourceRef);
  blendElement.addAttribute("mode", blendModeString);
}

bool ElementWriter::addColorFilterPrimitives(const std::shared_ptr<ColorFilter>& colorFilter,
                                             const std::string& inputResult,
                                             const std::string& outputResult) {
  if (!colorFilter) {
    return false;
  }
  switch (Types::Get(colorFilter.get())) {
    case Types::ColorFilterType::Matrix:
      addMatrixColorFilterPrimitives(static_cast<const MatrixColorFilter*>(colorFilter.get()),
                                     inputResult, outputResult);
      return true;
    case Types::ColorFilterType::Blend:
      addBlendColorFilterPrimitives(static_cast<const ModeColorFilter*>(colorFilter.get()),
                                    inputResult, outputResult);
      return true;
    case Types::ColorFilterType::Compose: {
      const auto compose = static_cast<const ComposeColorFilter*>(colorFilter.get());
      // Generate a unique result name to bridge inner's output to outer's input.
      // Without this, outer's Blend path falls back to SourceGraphic instead of inner's output.
      auto bridgeResult = resourceStore->addFilterResult();
      if (!addColorFilterPrimitives(compose->inner, inputResult, bridgeResult)) {
        return false;
      }
      return addColorFilterPrimitives(compose->outer, bridgeResult, outputResult);
    }
    case Types::ColorFilterType::Luma: {
      // tgfx Luma outputs vec4(luma) where luma = dot(rgb, vec3(Kr, Kg, Kb)).
      // All four channels (R, G, B, A) are set to the luminance value.
      // Use a custom feColorMatrix to replicate: R'=G'=B'=A' = Kr*R + Kg*G + Kb*B.
      ElementWriter colorMatrixElement("feColorMatrix", writer);
      if (!inputResult.empty()) {
        colorMatrixElement.addAttribute("in", inputResult);
      }
      colorMatrixElement.addAttribute("type", "matrix");
      colorMatrixElement.addAttribute("values",
                                      "0.2126 0.7152 0.0722 0 0 "
                                      "0.2126 0.7152 0.0722 0 0 "
                                      "0.2126 0.7152 0.0722 0 0 "
                                      "0.2126 0.7152 0.0722 0 0");
      if (!outputResult.empty()) {
        colorMatrixElement.addAttribute("result", outputResult);
      }
      return true;
    }
    case Types::ColorFilterType::AlphaThreshold: {
      const auto thresholdFilter = static_cast<const AlphaThresholdColorFilter*>(colorFilter.get());
      float threshold = thresholdFilter->threshold;
      ElementWriter transferElement("feComponentTransfer", writer);
      if (!inputResult.empty()) {
        transferElement.addAttribute("in", inputResult);
      }
      {
        ElementWriter funcA("feFuncA", writer);
        funcA.addAttribute("type", "discrete");
        // Generate a 256-entry discrete table: values below threshold map to 0, at or above to 1.
        int cutoff = FloatCeilToInt(threshold * 256.0f);
        if (cutoff < 0) {
          cutoff = 0;
        }
        if (cutoff > 256) {
          cutoff = 256;
        }
        std::string tableValues;
        for (int i = 0; i < 256; ++i) {
          if (i > 0) {
            tableValues += " ";
          }
          tableValues += (i < cutoff) ? "0" : "1";
        }
        funcA.addAttribute("tableValues", tableValues);
      }
      if (!outputResult.empty()) {
        transferElement.addAttribute("result", outputResult);
      }
      return true;
    }
    default:
      return false;
  }
}

void ElementWriter::addMatrixColorFilterPrimitives(const MatrixColorFilter* matrixColorFilter,
                                                   const std::string& inputResult,
                                                   const std::string& outputResult) {
  ElementWriter colorMatrixElement("feColorMatrix", writer);
  if (!inputResult.empty()) {
    colorMatrixElement.addAttribute("in", inputResult);
  }
  colorMatrixElement.addAttribute("type", "matrix");
  auto colorMatrix = matrixColorFilter->matrix;
  std::string matrixString;
  for (uint32_t i = 0; i < colorMatrix.size(); i++) {
    matrixString += FloatToString(colorMatrix[i]);
    if (i != 19) {
      matrixString += " ";
    }
  }
  colorMatrixElement.addAttribute("values", matrixString);
  if (!outputResult.empty()) {
    colorMatrixElement.addAttribute("result", outputResult);
  }
}

void ElementWriter::addBlendColorFilterPrimitives(const ModeColorFilter* modeColorFilter,
                                                  const std::string& inputResult,
                                                  const std::string& outputResult) {
  auto color = ConvertColorSpace(modeColorFilter->color, _targetColorSpace);
  // blendSource: the color data to blend against. In a Compose chain, this is inner's output.
  auto blendSource = inputResult.empty() ? std::string("SourceGraphic") : inputResult;

  // SVG feBlend modes include Porter-Duff compositing terms (e.g. src*(1-dstA)), which causes
  // color leakage into transparent regions. For Multiply mode, use feColorMatrix instead — it
  // performs pure per-channel multiplication without compositing, matching the GPU pipeline.
  if (modeColorFilter->mode == BlendMode::Multiply) {
    ElementWriter colorMatrixElement("feColorMatrix", writer);
    if (!inputResult.empty()) {
      colorMatrixElement.addAttribute("in", inputResult);
    }
    colorMatrixElement.addAttribute("type", "matrix");
    colorMatrixElement.addAttribute("values", FloatToString(color.red) + " 0 0 0 0 0 " +
                                                  FloatToString(color.green) + " 0 0 0 0 0 " +
                                                  FloatToString(color.blue) + " 0 0 0 0 0 " +
                                                  FloatToString(color.alpha) + " 0");
    if (!outputResult.empty()) {
      colorMatrixElement.addAttribute("result", outputResult);
    }
    return;
  }

  auto blendModeString = ToSVGBlendMode(modeColorFilter->mode);
  if (blendModeString.empty()) {
    reportUnsupportedElement("Unsupported blend mode in color filter");
    return;
  }
  {
    ElementWriter floodElement("feFlood", writer);
    floodElement.addAttribute("flood-color", ToSVGColor(color));
    floodElement.addAttribute("flood-opacity", color.alpha);
    floodElement.addAttribute("result", "flood");
    std::string cssStyle;
    if (writeColorCSSStyleAttribute("flood-color", color, &cssStyle)) {
      floodElement.addAttribute("style", cssStyle);
    }
  }
  {
    ElementWriter blendElement("feBlend", writer);
    blendElement.addAttribute("in", "flood");
    blendElement.addAttribute("in2", blendSource);
    blendElement.addAttribute("mode", blendModeString);
    blendElement.addAttribute("result", "blend");
  }
  {
    // Always clip to SourceGraphic alpha (the original shape boundary), not inner's potentially
    // alpha-modified output. GPU's ModeColorFilter operates per-pixel without an extra alpha clip
    // from inner — the shape boundary itself provides the clip.
    ElementWriter compositeElement("feComposite", writer);
    compositeElement.addAttribute("in", "blend");
    compositeElement.addAttribute("in2", "SourceGraphic");
    compositeElement.addAttribute("operator", "in");
    if (!outputResult.empty()) {
      compositeElement.addAttribute("result", outputResult);
    }
  }
}

Resources ElementWriter::addResources(const Brush& brush, Context* context,
                                      SVGExportContext* svgContext) {
  auto color = ConvertColorSpace(brush.color, _targetColorSpace);
  Resources resources(color);

  if (auto shader = brush.shader) {
    // Unwrap all Matrix and ColorFilter layers to find the leaf shader type.
    // Only PerlinNoise leaf shaders skip defs (they use filter primitives instead).
    const Shader* leaf = shader.get();
    while (leaf) {
      auto leafType = Types::Get(leaf);
      if (leafType == Types::ShaderType::Matrix) {
        leaf = static_cast<const MatrixShader*>(leaf)->source.get();
      } else if (leafType == Types::ShaderType::ColorFilter) {
        leaf = static_cast<const ColorFilterShader*>(leaf)->shader.get();
      } else {
        break;
      }
    }
    bool needsDefs = !leaf || Types::Get(leaf) != Types::ShaderType::PerlinNoise;
    if (needsDefs) {
      ElementWriter defs("defs", writer);
      addShaderResources(shader, context, &resources);
    } else {
      resources.filter = "pending";
    }
  }

  // Determine whether the shader produced filter primitives (PerlinNoise / ColorFilterShader)
  // that need to be combined with brush.colorFilter into a single <filter> element.
  // A "pending" value in resources.filter means the shader needs filter primitives but has not
  // yet emitted the <filter> element.
  bool shaderNeedsFilter = (resources.filter == "pending");
  bool hasColorFilter = brush.colorFilter != nullptr;

  if (shaderNeedsFilter || hasColorFilter) {
    resources.filter = "";
    std::string filterID = resourceStore->addFilter();
    {
      ElementWriter filterElement("filter", writer);
      filterElement.addAttribute("id", filterID);
      filterElement.addAttribute("color-interpolation-filters", "sRGB");
      filterElement.addAttribute("x", "0%");
      filterElement.addAttribute("y", "0%");
      filterElement.addAttribute("width", "100%");
      filterElement.addAttribute("height", "100%");
      if (shaderNeedsFilter) {
        addShaderFilterPrimitives(brush.shader.get());
      }
      if (hasColorFilter) {
        addColorFilterPrimitives(brush.colorFilter);
      }
    }
    resources.filter = "url(#" + filterID + ")";
  }

  if (auto maskFilter = brush.maskFilter) {
    addMaskResources(maskFilter, &resources, context, svgContext);
  }

  return resources;
}

static std::pair<const Shader*, Matrix> DecomposeShader(const std::shared_ptr<Shader>& shader) {
  Matrix matrix = {};
  const Shader* tempShader = shader.get();
  while (Types::Get(tempShader) == Types::ShaderType::Matrix) {
    auto matrixShader = static_cast<const MatrixShader*>(tempShader);
    matrix = matrix * matrixShader->matrix;
    tempShader = matrixShader->source.get();
  }
  return {tempShader, matrix};
}

void ElementWriter::addShaderResources(const std::shared_ptr<Shader>& shader, Context* context,
                                       Resources* resources) {
  auto [decomposedShader, matrix] = DecomposeShader(shader);

  auto type = Types::Get(decomposedShader);
  switch (type) {
    case Types::ShaderType::Color:
      addColorShaderResources(static_cast<const ColorShader*>(decomposedShader), resources);
      break;
    case Types::ShaderType::Gradient:
      addGradientShaderResources(static_cast<const GradientShader*>(decomposedShader), matrix,
                                 resources);
      break;
    case Types::ShaderType::Image:
      addImageShaderResources(static_cast<const ImageShader*>(decomposedShader), matrix, context,
                              resources);
      break;
    case Types::ShaderType::ColorFilter: {
      auto colorFilterShader = static_cast<const ColorFilterShader*>(decomposedShader);
      // Process the inner shader (may set fill/gradient/pattern resources).
      addShaderResources(colorFilterShader->shader, context, resources);
      // Mark that filter primitives are needed. The actual <filter> element will be emitted
      // by addResources, which may combine it with a brush colorFilter.
      if (colorFilterShader->colorFilter) {
        resources->filter = "pending";
      }
      break;
    }
    case Types::ShaderType::PerlinNoise: {
      // Mark that this shader needs a filter. The actual <filter> element will be emitted by
      // addResources, which may combine it with a brush colorFilter.
      resources->filter = "pending";
      break;
    }
    default:
      reportUnsupportedElement("Unsupported shader");
  }
}

void ElementWriter::addShaderFilterPrimitives(const Shader* shader) {
  const Shader* inner = shader;
  while (inner && Types::Get(inner) == Types::ShaderType::Matrix) {
    inner = static_cast<const MatrixShader*>(inner)->source.get();
  }
  if (!inner) {
    return;
  }
  auto type = Types::Get(inner);
  switch (type) {
    case Types::ShaderType::PerlinNoise:
      addPerlinNoisePrimitives(inner);
      break;
    case Types::ShaderType::ColorFilter: {
      auto colorFilterShader = static_cast<const ColorFilterShader*>(inner);
      // Recurse into the inner shader for its filter primitives.
      addShaderFilterPrimitives(colorFilterShader->shader.get());
      // Then append the ColorFilterShader's own color filter primitives.
      if (colorFilterShader->colorFilter) {
        addColorFilterPrimitives(colorFilterShader->colorFilter);
      }
      break;
    }
    default:
      break;
  }
}

void ElementWriter::addPerlinNoisePrimitives(const Shader* shader, const std::string& resultName) {
  auto noiseShader = static_cast<const PerlinNoiseShader*>(shader);
  ElementWriter turbulenceElement("feTurbulence", writer);
  turbulenceElement.addAttribute("type", noiseShader->noiseType == PerlinNoiseType::FractalNoise
                                             ? "fractalNoise"
                                             : "turbulence");
  turbulenceElement.addAttribute("baseFrequency", FloatToString(noiseShader->baseFrequencyX) + " " +
                                                      FloatToString(noiseShader->baseFrequencyY));
  turbulenceElement.addAttribute("numOctaves", noiseShader->numOctaves);
  turbulenceElement.addAttribute("seed", static_cast<int32_t>(noiseShader->seed));
  if (noiseShader->stitchTiles) {
    turbulenceElement.addAttribute("stitchTiles", "stitch");
  }
  if (!resultName.empty()) {
    turbulenceElement.addAttribute("result", resultName);
  }
}

void ElementWriter::addColorShaderResources(const ColorShader* shader, Resources* resources) {
  Color color;
  if (shader->asColor(&color)) {
    color = ConvertColorSpace(color, _targetColorSpace);
    resources->paintColor = ToSVGColor(color);
    resources->colorValue = color;
  }
}

void ElementWriter::addGradientShaderResources(const GradientShader* shader, const Matrix& matrix,
                                               Resources* resources) {
  GradientInfo info;
  GradientType type = shader->asGradient(&info);

  DEBUG_ASSERT(info.colors.size() == info.positions.size());
  if (type == GradientType::Linear) {
    resources->paintColor = "url(#" + addLinearGradientDef(info, matrix) + ")";
  } else if (type == GradientType::Radial) {
    resources->paintColor = "url(#" + addRadialGradientDef(info, matrix) + ")";
  } else {
    resources->paintColor = "url(#" + addUnsupportedGradientDef(info, matrix) + ")";
    reportUnsupportedElement("Unsupported gradient type");
  }
}

void ElementWriter::addGradientColors(const GradientInfo& info) {
  DEBUG_ASSERT(info.colors.size() >= 2);
  std::unique_ptr<ColorSpaceXformSteps> steps = nullptr;
  if (NeedConvertColorSpace(ColorSpace::SRGB(), _targetColorSpace)) {
    steps =
        std::make_unique<ColorSpaceXformSteps>(ColorSpace::SRGB().get(), AlphaType::Unpremultiplied,
                                               _targetColorSpace.get(), AlphaType::Unpremultiplied);
  }
  for (uint32_t i = 0; i < info.colors.size(); ++i) {
    auto color = info.colors[i];
    if (steps) {
      steps->apply(color.array());
    }
    auto colorStr = ToSVGColor(color);
    ElementWriter stop("stop", writer);
    stop.addAttribute("offset", info.positions[i]);
    stop.addAttribute("stop-color", colorStr);
    std::string cssStyle;
    if (writeColorCSSStyleAttribute("stop-color", color, &cssStyle)) {
      stop.addAttribute("style", cssStyle);
    }
    if (!color.isOpaque()) {
      stop.addAttribute("stop-opacity", color.alpha);
    }
  }
}

std::string ElementWriter::addLinearGradientDef(const GradientInfo& info, const Matrix& matrix) {
  DEBUG_ASSERT(resourceStore);
  auto id = resourceStore->addGradient();

  {
    ElementWriter gradient("linearGradient", writer);

    gradient.addAttribute("id", id);
    if (!matrix.isIdentity()) {
      gradient.addAttribute("gradientTransform", ToSVGTransform(matrix));
    }
    gradient.addAttribute("gradientUnits", "userSpaceOnUse");
    gradient.addAttribute("x1", info.points[0].x);
    gradient.addAttribute("y1", info.points[0].y);
    gradient.addAttribute("x2", info.points[1].x);
    gradient.addAttribute("y2", info.points[1].y);
    addGradientColors(info);
  }
  return id;
}

std::string ElementWriter::addRadialGradientDef(const GradientInfo& info, const Matrix& matrix) {
  DEBUG_ASSERT(resourceStore);
  auto id = resourceStore->addGradient();

  {
    ElementWriter gradient("radialGradient", writer);

    gradient.addAttribute("id", id);
    if (!matrix.isIdentity()) {
      gradient.addAttribute("gradientTransform", ToSVGTransform(matrix));
    }
    gradient.addAttribute("gradientUnits", "userSpaceOnUse");
    gradient.addAttribute("r", info.radiuses[0]);
    gradient.addAttribute("cx", info.points[0].x);
    gradient.addAttribute("cy", info.points[0].y);
    addGradientColors(info);
  }
  return id;
}

std::string ElementWriter::addUnsupportedGradientDef(const GradientInfo& info,
                                                     const Matrix& matrix) {
  DEBUG_ASSERT(resourceStore);
  auto id = resourceStore->addGradient();

  {
    ElementWriter gradient("radialGradient", writer);

    gradient.addAttribute("id", id);
    if (!matrix.isIdentity()) {
      gradient.addAttribute("gradientTransform", ToSVGTransform(matrix));
    }
    gradient.addAttribute("gradientUnits", "userSpaceOnUse");
    gradient.addAttribute("r", info.radiuses[0]);
    gradient.addAttribute("cx", info.points[0].x);
    gradient.addAttribute("cy", info.points[0].y);
    addGradientColors(info);
  }
  return id;
};

void ElementWriter::addImageShaderResources(const ImageShader* shader, const Matrix& matrix,
                                            Context* context, Resources* resources) {
  auto image = shader->image;
  DEBUG_ASSERT(image);
  image = ConvertImageColorSpace(image, context, _targetColorSpace, _assignColorSpace);
  auto dataUri = SVGExportContext::EncodeImageToDataUri(image, context);
  if (!dataUri) {
    return;
  }

  auto imageWidth = image->width();
  auto imageHeight = image->height();
  auto transDimension = [](TileMode mode, int length) -> std::string {
    if (mode == TileMode::Repeat) {
      return std::to_string(length);
    } else {
      return "100%";
    }
  };
  std::string widthValue = transDimension(shader->tileModeX, imageWidth);
  std::string heightValue = transDimension(shader->tileModeY, imageHeight);

  std::string patternID = resourceStore->addPattern();
  std::string imageID = resourceStore->addImage();
  {
    ElementWriter pattern("pattern", writer);
    pattern.addAttribute("id", patternID);
    pattern.addAttribute("patternUnits", "userSpaceOnUse");
    pattern.addAttribute("patternContentUnits", "userSpaceOnUse");
    pattern.addAttribute("width", widthValue);
    pattern.addAttribute("height", heightValue);
    pattern.addAttribute("x", 0);
    pattern.addAttribute("y", 0);

    {
      ElementWriter useTag("use", writer);
      useTag.addAttribute("xlink:href", "#" + imageID);
      if (!matrix.isIdentity()) {
        useTag.addAttribute("transform", ToSVGTransform(matrix));
      }
    }
  }
  {
    ElementWriter imageTag("image", writer);
    imageTag.addAttribute("id", imageID);
    imageTag.addAttribute("x", 0);
    imageTag.addAttribute("y", 0);
    imageTag.addAttribute("width", image->width());
    imageTag.addAttribute("height", image->height());
    imageTag.addAttribute("xlink:href", static_cast<const char*>(dataUri->data()));
  }
  resources->paintColor = "url(#" + patternID + ")";
}

void ElementWriter::addMaskResources(const std::shared_ptr<MaskFilter>& maskFilter,
                                     Resources* resources, Context* context,
                                     SVGExportContext* svgContext) {
  if (Types::Get(maskFilter.get()) != Types::MaskFilterType::Shader) {
    return;
  }

  auto maskShaderFilter = static_cast<const ShaderMaskFilter*>(maskFilter.get());

  bool inverse = maskShaderFilter->isInverted();
  std::string filterID;
  if (inverse) {
    writer->startElement("filter");
    filterID = resourceStore->addFilter();
    writer->addAttribute("id", filterID);
    {
      writer->startElement("feColorMatrix");
      writer->addAttribute("type", "matrix");
      writer->addAttribute("values", "1 0 0 0 0 0 1 0 0 0 0 0 1 0 0 0 0 0 -1 1");
      writer->endElement();
    }
    writer->endElement();
    filterID = "url(#" + filterID + ")";
  }

  auto maskShader = maskShaderFilter->getShader();
  auto type = Types::Get(maskShader.get());
  switch (type) {
    case Types::ShaderType::Image: {
      auto imageShader = static_cast<const ImageShader*>(maskShader.get());
      auto image = imageShader->image;

      ElementWriter maskElement("mask", writer);
      auto maskID = resourceStore->addMask();
      maskElement.addAttribute("id", maskID);
      maskElement.addAttribute("style", "mask-type:alpha");
      maskElement.addAttribute("maskUnits", "userSpaceOnUse");
      maskElement.addAttribute("width", "100%");
      maskElement.addAttribute("height", "100%");

      addImageMaskResources(imageShader, filterID, context, svgContext);

      resources->mask = "url(#" + maskID + ")";
      break;
    }
    case Types::ShaderType::Color:
    case Types::ShaderType::Gradient: {
      ElementWriter maskElement("mask", writer);
      auto maskID = resourceStore->addMask();
      maskElement.addAttribute("id", maskID);
      maskElement.addAttribute("style", "mask-type:alpha");
      maskElement.addAttribute("maskUnits", "userSpaceOnUse");
      maskElement.addAttribute("width", "100%");
      maskElement.addAttribute("height", "100%");

      addShaderMaskResources(maskShader, filterID, context);

      resources->mask = "url(#" + maskID + ")";
      break;
    }
    default:
      // TODO (YGaurora): The mask filter can be expanded to support shaders. Once shaders are
      // supported, the corresponding mask filter will also be supported.
      reportUnsupportedElement("unsupported mask filter");
  }
}

void ElementWriter::addImageMaskResources(const ImageShader* imageShader,
                                          const std::string& filterID, Context* context,
                                          SVGExportContext* svgContext) {
  auto image = imageShader->image;
  Types::ImageType type = Types::Get(image.get());
  if (type == Types::ImageType::Picture) {
    addPictureImageMaskResources(static_cast<const PictureImage*>(image.get()), filterID,
                                 svgContext);
  } else {
    addRenderImageMaskResources(imageShader, filterID, context);
  }
}

void ElementWriter::addPictureImageMaskResources(const PictureImage* pictureImage,
                                                 const std::string& filterID,
                                                 SVGExportContext* svgContext) {
  auto picture = pictureImage->picture;
  auto pictureBound = picture->getBounds();
  if (pictureImage->matrix) {
    pictureImage->matrix->mapRect(&pictureBound);
  }
  auto imageBound = Rect::MakeWH(pictureImage->width(), pictureImage->height());
  std::string clipID;
  if (!imageBound.contains(pictureBound)) {
    clipID = resourceStore->addClip();
    writer->startElement("clipPath");
    writer->addAttribute("id", clipID);
    {
      writer->startElement("rect");
      addRectAttributes(imageBound);
      writer->endElement();
    }
    writer->endElement();
  }

  auto matrix = Matrix::I();
  if (pictureImage->matrix) {
    matrix = *pictureImage->matrix;
  }

  writer->startElement("g");
  if (!clipID.empty()) {
    writer->addAttribute("clip-path", "url(#" + clipID + ")");
  }
  if (!filterID.empty()) {
    writer->addAttribute("filter", filterID);
  }
  svgContext->drawPicture(picture, matrix, ClipStack());
  writer->endElement();
}

void ElementWriter::addRenderImageMaskResources(const ImageShader* imageShader,
                                                const std::string& filterID, Context* context) {
  Resources resources;
  addImageShaderResources(imageShader, {}, context, &resources);

  writer->startElement("rect");
  addAttribute("fill", resources.paintColor);
  if (!filterID.empty()) {
    writer->addAttribute("filter", filterID);
  }
  addAttribute("width", "100%");
  addAttribute("height", "100%");
  writer->endElement();
}

void ElementWriter::addShaderMaskResources(const std::shared_ptr<Shader>& shader,
                                           const std::string& filterID, Context* context) {
  Resources resources;
  addShaderResources(shader, context, &resources);

  writer->startElement("rect");
  addAttribute("fill", resources.paintColor);
  if (resources.paintColor.find("url") == std::string::npos) {
    std::string cssStyle;
    if (writeColorCSSStyleAttribute("fill", resources.colorValue, &cssStyle)) {
      addAttribute("style", cssStyle);
    }
  }
  if (!filterID.empty()) {
    writer->addAttribute("filter", filterID);
  }
  addAttribute("width", "100%");
  addAttribute("height", "100%");
  writer->endElement();
}

}  // namespace tgfx
