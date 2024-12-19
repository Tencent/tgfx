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

#include "ElementWriter.h"
#include <string>
#include <unordered_set>
#include "SVGExportingContext.h"
#include "SVGUtils.h"
#include "core/CanvasState.h"
#include "core/utils/Caster.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/GradientType.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Size.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {

Resources::Resources(const FillStyle& fill) {
  paintColor = ToSVGColor(fill.color);
}

ElementWriter::ElementWriter(const std::string& name, XMLWriter* writer) : writer(writer) {
  writer->startElement(name);
}

ElementWriter::ElementWriter(const std::string& name, const std::unique_ptr<XMLWriter>& writer)
    : ElementWriter(name, writer.get()) {
}

ElementWriter::ElementWriter(const std::string& name, const std::unique_ptr<XMLWriter>& writer,
                             ResourceStore* bucket)
    : writer(writer.get()), resourceStore(bucket) {
  writer->startElement(name);
}

ElementWriter::ElementWriter(const std::string& name, Context* context,
                             const std::unique_ptr<XMLWriter>& writer, ResourceStore* bucket,
                             bool disableWarning, const MCState& state, const FillStyle& fill,
                             const Stroke* stroke)
    : writer(writer.get()), resourceStore(bucket), disableWarning(disableWarning) {
  Resources res = this->addResources(fill, context);

  writer->startElement(name);

  this->addFillAndStroke(fill, stroke, res);

  if (!state.matrix.isIdentity()) {
    this->addAttribute("transform", ToSVGTransform(state.matrix));
  }
}

ElementWriter::~ElementWriter() {
  writer->endElement();
  resourceStore = nullptr;
}

void ElementWriter::reportUnsupportedElement(const char* message) const {
  if (!disableWarning) {
    LOGE("[SVG exporting]:Unsupported %s", message);
  }
}

void ElementWriter::addFillAndStroke(const FillStyle& fill, const Stroke* stroke,
                                     const Resources& resources) {
  if (!stroke) {  //fill draw
    static const std::string defaultFill = "black";
    if (resources.paintColor != defaultFill) {
      this->addAttribute("fill", resources.paintColor);
    }
    if (!fill.isOpaque()) {
      this->addAttribute("fill-opacity", fill.color.alpha);
    }
  } else {  //stroke draw
    this->addAttribute("fill", "none");

    this->addAttribute("stroke", resources.paintColor);

    float strokeWidth = stroke->width;
    if (strokeWidth == 0) {
      // Hairline stroke
      strokeWidth = 1;
      this->addAttribute("vector-effect", "non-scaling-stroke");
    }
    this->addAttribute("stroke-width", strokeWidth);

    auto cap = ToSVGCap(stroke->cap);
    if (!cap.empty()) {
      this->addAttribute("stroke-linecap", cap);
    }

    auto join = ToSVGJoin(stroke->join);
    if (!join.empty()) {
      this->addAttribute("stroke-linejoin", join);
    }

    if (stroke->join == LineJoin::Miter && !FloatNearlyEqual(stroke->miterLimit, 4.f)) {
      this->addAttribute("stroke-miterlimit", stroke->miterLimit);
    }

    if (!fill.isOpaque()) {
      this->addAttribute("stroke-opacity", fill.color.alpha);
    }
  }

  if (fill.blendMode != BlendMode::SrcOver) {
    auto blendModeString = ToSVGBlendMode(fill.blendMode);
    if (!blendModeString.empty()) {
      this->addAttribute("style", blendModeString);
    } else {
      reportUnsupportedElement("blend mode");
    }
  }

  if (!resources.filter.empty()) {
    this->addAttribute("filter", resources.filter);
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
  this->addAttribute("font-size", font.getSize());

  auto typeface = font.getTypeface();
  if (typeface == nullptr) {
    return;
  }
  auto familyName = typeface->fontFamily();
  if (!familyName.empty()) {
    this->addAttribute("font-family", familyName);
  }

  if (font.isFauxItalic()) {
    this->addAttribute("font-style", "italic");
  }
  if (font.isFauxBold()) {
    this->addAttribute("font-weight", "bold");
  }
}

void ElementWriter::addRectAttributes(const Rect& rect) {
  // x, y default to 0
  if (rect.x() != 0) {
    this->addAttribute("x", rect.x());
  }
  if (rect.y() != 0) {
    this->addAttribute("y", rect.y());
  }

  this->addAttribute("width", rect.width());
  this->addAttribute("height", rect.height());
}

void ElementWriter::addRoundRectAttributes(const RRect& roundRect) {
  this->addRectAttributes(roundRect.rect);
  if (FloatNearlyZero(roundRect.radii.x) && FloatNearlyZero(roundRect.radii.y)) {
    return;
  }
  this->addAttribute("rx", roundRect.radii.x);
  this->addAttribute("ry", roundRect.radii.y);
}

void ElementWriter::addCircleAttributes(const Rect& bound) {
  this->addAttribute("cx", bound.centerX());
  this->addAttribute("cy", bound.centerY());
  this->addAttribute("r", bound.width() * 0.5f);
}

void ElementWriter::addEllipseAttributes(const Rect& bound) {
  this->addAttribute("cx", bound.centerX());
  this->addAttribute("cy", bound.centerY());
  this->addAttribute("rx", bound.width() * 0.5f);
  this->addAttribute("ry", bound.height() * 0.5f);
}

void ElementWriter::addPathAttributes(const Path& path, PathEncoding encoding) {
  this->addAttribute("d", ToSVGPath(path, encoding));
}

Resources ElementWriter::addImageFilterResource(const std::shared_ptr<ImageFilter>& imageFilter,
                                                Rect bound) {
  std::string filterID = resourceStore->addFilter();
  {
    ElementWriter filterElement("filter", writer);
    filterElement.addAttribute("id", filterID);
    if (auto blurFilter = ImageFilterCaster::AsBlurImageFilter(imageFilter)) {
      bound = blurFilter->filterBounds(bound);
      filterElement.addAttribute("x", bound.x());
      filterElement.addAttribute("y", bound.y());
      filterElement.addAttribute("width", bound.width());
      filterElement.addAttribute("height", bound.height());
      filterElement.addAttribute("filterUnits", "userSpaceOnUse");
      addBlurImageFilter(blurFilter);
    } else if (auto dropShadowFilter = ImageFilterCaster::AsDropShadowImageFilter(imageFilter)) {
      bound = blurFilter->filterBounds(bound);
      filterElement.addAttribute("x", bound.x());
      filterElement.addAttribute("y", bound.y());
      filterElement.addAttribute("width", bound.width());
      filterElement.addAttribute("height", bound.height());
      filterElement.addAttribute("filterUnits", "userSpaceOnUse");
      addDropShadowImageFilter(dropShadowFilter);
    } else if (auto innerShadowFilter = ImageFilterCaster::AsInnerShadowImageFilter(imageFilter)) {
      bound = blurFilter->filterBounds(bound);
      filterElement.addAttribute("x", bound.x());
      filterElement.addAttribute("y", bound.y());
      filterElement.addAttribute("width", bound.width() + innerShadowFilter->dx);
      filterElement.addAttribute("height", bound.height() + innerShadowFilter->dy);
      filterElement.addAttribute("filterUnits", "userSpaceOnUse");
      addInnerShadowImageFilter(innerShadowFilter);
    } else {
      reportUnsupportedElement("image filter");
    }
  }
  Resources resources;
  resources.filter = "url(#" + filterID + ")";
  return resources;
}

void ElementWriter::addBlurImageFilter(const std::shared_ptr<const BlurImageFilter>& filter) {
  ElementWriter blurElement("feGaussianBlur", writer);
  auto blurSize = filter->filterBounds(Rect::MakeEmpty()).size();
  blurElement.addAttribute("stdDeviation", std::max(blurSize.width / 4.f, blurSize.height / 4.f));
  blurElement.addAttribute("result", "blur");
}

void ElementWriter::addDropShadowImageFilter(
    const std::shared_ptr<const DropShadowImageFilter>& filter) {
  if (!filter->blurFilter) {
    return;
  }
  {
    ElementWriter offsetElement("feOffset", writer);
    offsetElement.addAttribute("dx", filter->dx);
    offsetElement.addAttribute("dy", filter->dy);
  }
  {
    ElementWriter blurElement("feGaussianBlur", writer);
    auto blurSize = filter->blurFilter->filterBounds(Rect::MakeEmpty()).size();
    blurElement.addAttribute("stdDeviation", std::max(blurSize.width / 4.f, blurSize.height / 4.f));
    blurElement.addAttribute("result", "blur");
  }
  {
    ElementWriter colorMatrixElement("feColorMatrix", writer);
    colorMatrixElement.addAttribute("type", "matrix");
    auto color = filter->color;
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
void ElementWriter::addInnerShadowImageFilter(
    const std::shared_ptr<const InnerShadowImageFilter>& filter) {
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
    auto blurSize = filter->blurFilter->filterBounds(Rect::MakeEmpty()).size();
    blurElement.addAttribute("stdDeviation", std::max(blurSize.width / 4.f, blurSize.height / 4.f));
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

Resources ElementWriter::addResources(const FillStyle& fill, Context* context) {
  Resources resources(fill);

  if (auto shader = fill.shader) {
    ElementWriter defs("defs", writer);
    this->addShaderResources(shader, context, &resources);
  }

  if (auto colorFilter = fill.colorFilter) {
    // TODO(YGAurora): Implement skia color filters for blend modes other than SrcIn
    BlendMode mode;
    if (colorFilter->asColorMode(nullptr, &mode) && mode == BlendMode::SrcIn) {
      this->addColorFilterResources(*colorFilter, &resources);
    } else {
      reportUnsupportedElement("blend mode in color filter");
    }
  }

  return resources;
}

void ElementWriter::addShaderResources(const std::shared_ptr<Shader>& shader, Context* context,
                                       Resources* resources) {
  if (auto colorShader = ShaderCaster::AsColorShader(shader)) {
    this->addColorShaderResources(colorShader, resources);
  } else if (auto gradientShader = ShaderCaster::AsGradientShader(shader)) {
    this->addGradientShaderResources(gradientShader, resources);
  } else if (auto imageShader = ShaderCaster::AsImageShader(shader)) {
    this->addImageShaderResources(imageShader, context, resources);
  } else {
    reportUnsupportedElement("shader");
  }
  // TODO(YGAurora): other shader types
}

void ElementWriter::addColorShaderResources(const std::shared_ptr<const ColorShader>& shader,
                                            Resources* resources) {
  Color color;
  if (shader->asColor(&color)) {
    resources->paintColor = ToSVGColor(color);
  }
}

void ElementWriter::addGradientShaderResources(const std::shared_ptr<const GradientShader>& shader,
                                               Resources* resources) {
  GradientInfo info;
  GradientType type = shader->asGradient(&info);

  DEBUG_ASSERT(info.colors.size() == info.positions.size());
  if (type == GradientType::Linear) {
    resources->paintColor = "url(#" + addLinearGradientDef(info) + ")";
  } else if (type == GradientType::Radial) {
    resources->paintColor = "url(#" + addRadialGradientDef(info) + ")";
  } else {
    resources->paintColor = "url(#" + addUnsupportedGradientDef(info) + ")";
    reportUnsupportedElement("gradient type");
  }
}

void ElementWriter::addGradientColors(const GradientInfo& info) {
  DEBUG_ASSERT(info.colors.size() >= 2);
  for (uint32_t i = 0; i < info.colors.size(); ++i) {
    auto color = info.colors[i];
    auto colorStr = ToSVGColor(color);

    ElementWriter stop("stop", writer);
    stop.addAttribute("offset", info.positions[i]);
    stop.addAttribute("stop-color", colorStr);

    if (!color.isOpaque()) {
      stop.addAttribute("stop-opacity", color.alpha);
    }
  }
}

std::string ElementWriter::addLinearGradientDef(const GradientInfo& info) {
  DEBUG_ASSERT(resourceStore);
  auto id = resourceStore->addGradient();

  {
    ElementWriter gradient("linearGradient", writer);

    gradient.addAttribute("id", id);
    gradient.addAttribute("gradientUnits", "userSpaceOnUse");
    gradient.addAttribute("x1", info.points[0].x);
    gradient.addAttribute("y1", info.points[0].y);
    gradient.addAttribute("x2", info.points[1].x);
    gradient.addAttribute("y2", info.points[1].y);
    addGradientColors(info);
  }
  return id;
}

std::string ElementWriter::addRadialGradientDef(const GradientInfo& info) {
  DEBUG_ASSERT(resourceStore);
  auto id = resourceStore->addGradient();

  {
    ElementWriter gradient("radialGradient", writer);

    gradient.addAttribute("id", id);
    gradient.addAttribute("gradientUnits", "userSpaceOnUse");
    gradient.addAttribute("r", info.radiuses[0]);
    gradient.addAttribute("cx", info.points[0].x);
    gradient.addAttribute("cy", info.points[0].y);
    addGradientColors(info);
  }
  return id;
}

std::string ElementWriter::addUnsupportedGradientDef(const GradientInfo& info) {
  DEBUG_ASSERT(resourceStore);
  auto id = resourceStore->addGradient();

  {
    ElementWriter gradient("linearGradient", writer);

    gradient.addAttribute("id", id);
    gradient.addAttribute("gradientUnits", "objectBoundingBox");
    gradient.addAttribute("x1", 0);
    gradient.addAttribute("y1", 0);
    gradient.addAttribute("x2", 1);
    gradient.addAttribute("y2", 0);
    addGradientColors(info);
  }
  return id;
};

void ElementWriter::addImageShaderResources(const std::shared_ptr<const ImageShader>& shader,
                                            Context* context, Resources* resources) {
  auto image = shader->image;
  DEBUG_ASSERT(shader->image);

  DEBUG_ASSERT(context);
  Bitmap bitmap(image->width(), image->height(), false, false);
  Pixmap pixmap(bitmap);
  if (!SVGExportingContext::exportToPixmap(context, image, pixmap)) {
    return;
  }
  auto dataUri = AsDataUri(pixmap);
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
      std::string imageID = resourceStore->addImage();
      ElementWriter imageTag("image", writer);
      imageTag.addAttribute("id", imageID);
      imageTag.addAttribute("x", 0);
      imageTag.addAttribute("y", 0);
      imageTag.addAttribute("width", image->width());
      imageTag.addAttribute("height", image->height());
      imageTag.addAttribute("xlink:href", static_cast<const char*>(dataUri->data()));
    }
  }
  resources->paintColor = "url(#" + patternID + ")";
}

void ElementWriter::addColorFilterResources(const ColorFilter& colorFilter, Resources* resources) {
  std::string filterID = resourceStore->addFilter();
  {
    ElementWriter filterElement("filter", writer);
    filterElement.addAttribute("id", filterID);
    filterElement.addAttribute("x", "0%");
    filterElement.addAttribute("y", "0%");
    filterElement.addAttribute("width", "100%");
    filterElement.addAttribute("height", "100%");

    Color filterColor;
    BlendMode mode;
    colorFilter.asColorMode(&filterColor, &mode);

    {
      // first flood with filter color
      ElementWriter floodElement("feFlood", writer);
      floodElement.addAttribute("flood-color", ToSVGColor(filterColor));
      floodElement.addAttribute("flood-opacity", filterColor.alpha);
      floodElement.addAttribute("result", "flood");
    }

    {
      // apply the transform to filter color
      ElementWriter compositeElement("feComposite", writer);
      compositeElement.addAttribute("in", "flood");
      compositeElement.addAttribute("operator", "in");
    }
  }
  resources->filter = "url(#" + filterID + ")";
}
}  // namespace tgfx