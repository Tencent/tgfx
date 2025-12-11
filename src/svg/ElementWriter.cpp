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
#include "core/filters/ComposeImageFilter.h"
#include "core/filters/GaussianBlurImageFilter.h"
#include "core/filters/ShaderMaskFilter.h"
#include "core/shaders/MatrixShader.h"
#include "core/utils/ColorHelper.h"
#include "core/utils/ColorSpaceHelper.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "core/utils/Types.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/GradientType.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Pixmap.h"
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
                             std::shared_ptr<ColorSpace> writeColorSpace)
    : writer(writer), _targetColorSpace(std::move(targetColorSpace)),
      _writeColorSpace(std::move(writeColorSpace)) {
  writer->startElement(name);
}

ElementWriter::ElementWriter(const std::string& name, const std::unique_ptr<XMLWriter>& writer,
                             std::shared_ptr<ColorSpace> targetColorSpace,
                             std::shared_ptr<ColorSpace> writeColorSpace)
    : ElementWriter(name, writer.get(), std::move(targetColorSpace), std::move(writeColorSpace)) {
}

ElementWriter::ElementWriter(const std::string& name, const std::unique_ptr<XMLWriter>& writer,
                             ResourceStore* bucket, std::shared_ptr<ColorSpace> targetColorSpace,
                             std::shared_ptr<ColorSpace> writeColorSpace)
    : writer(writer.get()), resourceStore(bucket), _targetColorSpace(std::move(targetColorSpace)),
      _writeColorSpace(std::move(writeColorSpace)) {
  writer->startElement(name);
}

ElementWriter::ElementWriter(const std::string& name, Context* context,
                             SVGExportContext* svgContext, XMLWriter* writer, ResourceStore* bucket,
                             bool disableWarning, const MCState& state, const Brush& brush,
                             const Stroke* stroke, std::shared_ptr<ColorSpace> targetColorSpace,
                             std::shared_ptr<ColorSpace> writeColorSpace)
    : writer(writer), resourceStore(bucket), disableWarning(disableWarning),
      _targetColorSpace(std::move(targetColorSpace)), _writeColorSpace(std::move(writeColorSpace)) {
  generateWriteColorSpaceString();
  Resources resource = addResources(brush, context, svgContext);

  writer->startElement(name);

  addFillAndStroke(brush, stroke, resource);

  if (!state.matrix.isIdentity()) {
    addAttribute("transform", ToSVGTransform(state.matrix));
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
                _writeColorSpaceString + " " + std::to_string(color.red) + " " +
                std::to_string(color.green) + " " + std::to_string(color.blue) + ");";
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
  if (ColorSpace::Equals(_writeColorSpace.get(), displayP3.get())) {
    _writeColorSpaceString = "display-p3";
  } else if (ColorSpace::Equals(_writeColorSpace.get(), a98rgb.get())) {
    _writeColorSpaceString = "a98-rgb";
  } else if (ColorSpace::Equals(_writeColorSpace.get(), rec2020.get())) {
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
  addRectAttributes(roundRect.rect);
  if (FloatNearlyZero(roundRect.radii.x) && FloatNearlyZero(roundRect.radii.y)) {
    return;
  }
  addAttribute("rx", roundRect.radii.x);
  addAttribute("ry", roundRect.radii.y);
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

Resources ElementWriter::addImageFilterResource(
    const std::shared_ptr<ImageFilter>& imageFilter, Rect bound,
    const std::shared_ptr<SVGCustomWriter>& exportWriter) {
  auto filterID = addImageFilter(imageFilter, bound, std::move(exportWriter));
  Resources resources;
  resources.filter = "url(#" + filterID + ")";
  return resources;
}

std::string ElementWriter::addImageFilter(const std::shared_ptr<ImageFilter>& imageFilter,
                                          Rect bound,
                                          const std::shared_ptr<SVGCustomWriter>& exportWriter) {
  auto type = Types::Get(imageFilter.get());
  switch (type) {
    case Types::ImageFilterType::Blur: {
      const auto blurFilter = static_cast<const GaussianBlurImageFilter*>(imageFilter.get());
      bound = blurFilter->filterBounds(bound);
      std::string filterID = resourceStore->addFilter();
      ElementWriter filterElement("filter", writer);
      filterElement.addAttribute("id", filterID);
      filterElement.addAttribute("x", bound.x());
      filterElement.addAttribute("y", bound.y());
      filterElement.addAttribute("width", bound.width());
      filterElement.addAttribute("height", bound.height());
      filterElement.addAttribute("filterUnits", "userSpaceOnUse");
      callbackBlurImageFilter(blurFilter, exportWriter, filterElement);
      addBlurImageFilter(blurFilter);
      return filterID;
    }
    case Types::ImageFilterType::DropShadow: {
      const auto dropShadowFilter = static_cast<const DropShadowImageFilter*>(imageFilter.get());
      bound = dropShadowFilter->filterBounds(bound);
      std::string filterID = resourceStore->addFilter();
      ElementWriter filterElement("filter", writer);
      filterElement.addAttribute("id", filterID);
      filterElement.addAttribute("x", bound.x());
      filterElement.addAttribute("y", bound.y());
      filterElement.addAttribute("width", bound.width());
      filterElement.addAttribute("height", bound.height());
      filterElement.addAttribute("filterUnits", "userSpaceOnUse");
      callbackDropShadowImageFilter(dropShadowFilter, exportWriter, filterElement);
      addDropShadowImageFilter(dropShadowFilter);
      return filterID;
    }
    case Types::ImageFilterType::InnerShadow: {
      const auto innerShadowFilter = static_cast<const InnerShadowImageFilter*>(imageFilter.get());
      bound = innerShadowFilter->filterBounds(bound);
      std::string filterID = resourceStore->addFilter();
      ElementWriter filterElement("filter", writer);
      filterElement.addAttribute("id", filterID);
      filterElement.addAttribute("x", bound.x());
      filterElement.addAttribute("y", bound.y());
      filterElement.addAttribute("width", bound.width() + innerShadowFilter->dx);
      filterElement.addAttribute("height", bound.height() + innerShadowFilter->dy);
      filterElement.addAttribute("filterUnits", "userSpaceOnUse");
      callbackInnerShadowImageFilter(innerShadowFilter, exportWriter, filterElement);
      addInnerShadowImageFilter(innerShadowFilter);
      return filterID;
    }
    case Types::ImageFilterType::Compose: {
      const auto composeFilter = static_cast<const ComposeImageFilter*>(imageFilter.get());
      std::string filterID;
      for (const auto& filterItem : composeFilter->filters) {
        auto id = addImageFilter(filterItem, bound, exportWriter);
        if (!id.empty()) {
          filterID = id;
        }
      }
      return filterID;
    }
    default:
      reportUnsupportedElement("Unsupported image filter");
      return "";
  }
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

void ElementWriter::addBlurImageFilter(const GaussianBlurImageFilter* filter) {
  ElementWriter blurElement("feGaussianBlur", writer);
  blurElement.addAttribute("stdDeviation",
                           std::max(filter->blurrinessX, filter->blurrinessY) / 2.f);
  blurElement.addAttribute("result", "blur");
}

void ElementWriter::addDropShadowImageFilter(const DropShadowImageFilter* filter) {
  {
    ElementWriter offsetElement("feOffset", writer);
    offsetElement.addAttribute("dx", filter->dx);
    offsetElement.addAttribute("dy", filter->dy);
  }
  {
    ElementWriter blurElement("feGaussianBlur", writer);
    float blurriness = 0.f;
    if (filter->blurFilter) {
      if (Types::Get(filter->blurFilter.get()) == Types::ImageFilterType::Blur) {
        const auto blurFilter =
            static_cast<const GaussianBlurImageFilter*>(filter->blurFilter.get());
        blurriness = std::max(blurFilter->blurrinessX, blurFilter->blurrinessY) / 2.f;
      }
    }
    blurElement.addAttribute("stdDeviation", blurriness);
    blurElement.addAttribute("result", "blur");
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
  }
  if (!filter->shadowOnly) {
    ElementWriter blendElement("feBlend", writer);
    blendElement.addAttribute("mode", "normal");
    blendElement.addAttribute("in", "SourceGraphic");
  }
}
void ElementWriter::addInnerShadowImageFilter(const InnerShadowImageFilter* filter) {
  if (!filter->blurFilter) {
    return;
  }
  {
    ElementWriter colorMatrixElement("feColorMatrix", writer);
    colorMatrixElement.addAttribute("in", "SourceAlpha");
    colorMatrixElement.addAttribute("type", "matrix");
    colorMatrixElement.addAttribute("values", "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 127 0");
    colorMatrixElement.addAttribute("result", "hardAlpha");
  }
  if (!filter->shadowOnly) {
    {
      ElementWriter floodElement("feFlood", writer);
      floodElement.addAttribute("flood-opacity", "0");
      floodElement.addAttribute("result", "BackgroundImageFix");
    }
    {
      ElementWriter blendElement("feBlend", writer);
      blendElement.addAttribute("mode", "normal");
      blendElement.addAttribute("in", "SourceGraphic");
      blendElement.addAttribute("in2", "BackgroundImageFix");
      blendElement.addAttribute("result", "shape");
    }
  }
  {
    ElementWriter offsetElement("feOffset", writer);
    offsetElement.addAttribute("dx", filter->dx);
    offsetElement.addAttribute("dy", filter->dy);
  }
  {
    ElementWriter blurElement("feGaussianBlur", writer);
    if (Types::Get(filter->blurFilter.get()) == Types::ImageFilterType::Blur) {
      auto blurFilter = static_cast<const GaussianBlurImageFilter*>(filter->blurFilter.get());
      blurElement.addAttribute("stdDeviation",
                               std::max(blurFilter->blurrinessX, blurFilter->blurrinessY) / 2.f);
    }
  }
  {
    ElementWriter compositeElement("feComposite", writer);
    compositeElement.addAttribute("in2", "hardAlpha");
    compositeElement.addAttribute("operator", "arithmetic");
    compositeElement.addAttribute("k2", "-1");
    compositeElement.addAttribute("k3", "1");
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
  }
  if (!filter->shadowOnly) {
    ElementWriter blendElement("feBlend", writer);
    blendElement.addAttribute("mode", "normal");
    blendElement.addAttribute("in2", "shape");
  }
}

Resources ElementWriter::addResources(const Brush& brush, Context* context,
                                      SVGExportContext* svgContext) {
  auto color = ConvertColorSpace(brush.color, _targetColorSpace);
  Resources resources(color);

  if (auto shader = brush.shader) {
    ElementWriter defs("defs", writer);
    addShaderResources(shader, context, &resources);
  }

  if (auto colorFilter = brush.colorFilter) {
    auto type = Types::Get(colorFilter.get());
    switch (type) {
      case Types::ColorFilterType::Blend:
        addBlendColorFilterResources(static_cast<const ModeColorFilter*>(colorFilter.get()),
                                     &resources);
        break;
      case Types::ColorFilterType::Matrix: {
        addMatrixColorFilterResources(static_cast<const MatrixColorFilter*>(colorFilter.get()),
                                      &resources);
        break;
      }
      default:
        reportUnsupportedElement("Unsupported color filter");
    }
  }

  if (auto maskFilter = brush.maskFilter) {
    addMaskResources(maskFilter, &resources, context, svgContext);
  }

  return resources;
}

void ElementWriter::addShaderResources(const std::shared_ptr<Shader>& shader, Context* context,
                                       Resources* resources) {
  auto shaderDecomposer =
      [](const std::shared_ptr<Shader>& decomposedShader) -> std::pair<const Shader*, Matrix> {
    Matrix matrix = {};
    const Shader* tempShader = decomposedShader.get();

    while (Types::Get(tempShader) == Types::ShaderType::Matrix) {
      auto matrixShader = static_cast<const MatrixShader*>(tempShader);
      matrix = matrix * matrixShader->matrix;
      tempShader = matrixShader->source.get();
    };
    return {tempShader, matrix};
  };
  auto [decomposedShader, matrix] = shaderDecomposer(shader);

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
    default:
      // TODO(YGaurora):
      // Export color filter shaders as color filters.
      // Export blend shaders as a combination of a shader and blend mode.
      reportUnsupportedElement("Unsupported shader");
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

  image = ModifyImage(image, context, _targetColorSpace, _writeColorSpace);
  std::shared_ptr<Data> dataUri = nullptr;

  auto data = SVGExportContext::ImageToEncodedData(image);
  if (data && (JpegCodec::IsJpeg(data) || PngCodec::IsPng(data))) {
    dataUri = AsDataUri(data);
  } else {
    Bitmap bitmap = SVGExportContext::ImageExportToBitmap(context, image);
    if (bitmap.isEmpty()) {
      return;
    }
    dataUri = AsDataUri(Pixmap(bitmap));
  }

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

void ElementWriter::addBlendColorFilterResources(const ModeColorFilter* modeColorFilter,
                                                 Resources* resources) {

  auto BlendModeString = ToSVGBlendMode(modeColorFilter->mode);
  if (BlendModeString.empty()) {
    reportUnsupportedElement("Unsupported blend mode in color filter");
    return;
  }

  std::string filterID = resourceStore->addFilter();
  {
    ElementWriter filterElement("filter", writer);
    filterElement.addAttribute("id", filterID);
    filterElement.addAttribute("x", "0%");
    filterElement.addAttribute("y", "0%");
    filterElement.addAttribute("width", "100%");
    filterElement.addAttribute("height", "100%");

    {
      // first flood with filter color
      ElementWriter floodElement("feFlood", writer);
      auto color = ConvertColorSpace(modeColorFilter->color, _targetColorSpace);
      floodElement.addAttribute("flood-color", ToSVGColor(color));
      floodElement.addAttribute("flood-opacity", modeColorFilter->color.alpha);
      floodElement.addAttribute("result", "flood");
      std::string cssStyle;
      if (writeColorCSSStyleAttribute("flood-color", color, &cssStyle)) {
        floodElement.addAttribute("style", cssStyle);
      }
    }

    {
      ElementWriter blendElement("feBlend", writer);
      blendElement.addAttribute("in", "flood");
      blendElement.addAttribute("in2", "SourceGraphic");
      blendElement.addAttribute("mode", BlendModeString);
      blendElement.addAttribute("result", "blend");
    }

    {
      // apply the transform to filter color
      ElementWriter compositeElement("feComposite", writer);
      compositeElement.addAttribute("in", "blend");
      compositeElement.addAttribute("operator", "in");
    }
  }
  resources->filter = "url(#" + filterID + ")";
}

void ElementWriter::addMatrixColorFilterResources(const MatrixColorFilter* matrixColorFilter,
                                                  Resources* resources) {
  std::string filterID = resourceStore->addFilter();
  {
    ElementWriter filterElement("filter", writer);
    filterElement.addAttribute("id", filterID);
    filterElement.addAttribute("x", "0%");
    filterElement.addAttribute("y", "0%");
    filterElement.addAttribute("width", "100%");
    filterElement.addAttribute("height", "100%");

    {
      ElementWriter colorMatrixElement("feColorMatrix", writer);
      colorMatrixElement.addAttribute("in", "SourceGraphic");
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
    }
  }
  resources->filter = "url(#" + filterID + ")";
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

  MCState state;
  if (pictureImage->matrix) {
    state.matrix = *pictureImage->matrix;
  }

  writer->startElement("g");
  if (!clipID.empty()) {
    writer->addAttribute("clip-path", "url(#" + clipID + ")");
  }
  if (!filterID.empty()) {
    writer->addAttribute("filter", filterID);
  }
  svgContext->drawPicture(picture, state);
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
