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
#include "SVGContext.h"
#include "SVGUtils.h"
#include "core/MCState.h"
#include "core/filters/ImageFilterBase.h"
#include "core/shaders/ShaderBase.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Surface.h"

namespace tgfx {

Resources::Resources(const FillStyle& fill) {
  _paintColor = ToSVGColor(fill.color);
}

ElementWriter::ElementWriter(const std::string& name, Context* GPUContext, XMLWriter* writer)
    : _context(GPUContext), _writer(writer) {
  _writer->startElement(name);
}

ElementWriter::ElementWriter(const std::string& name, Context* GPUContext,
                             const std::unique_ptr<XMLWriter>& writer)
    : ElementWriter(name, GPUContext, writer.get()) {
}

ElementWriter::ElementWriter(const std::string& name, Context* GPUContext,
                             const std::unique_ptr<XMLWriter>& writer, ResourceStore* bucket)
    : _context(GPUContext), _writer(writer.get()), _resourceStore(bucket) {
  _writer->startElement(name);
}

ElementWriter::ElementWriter(const std::string& name, Context* GPUContext, SVGContext* svgContext,
                             ResourceStore* bucket, const MCState& state, const FillStyle& fill,
                             const Stroke* stroke)
    : _context(GPUContext), _writer(svgContext->getWriter()), _resourceStore(bucket) {

  svgContext->syncMCState(state);
  Resources res = this->addResources(fill);

  _writer->startElement(name);

  this->addFillAndStroke(fill, stroke, res);

  if (!state.matrix.isIdentity()) {
    this->addAttribute("transform", ToSVGTransform(state.matrix));
  }
}

ElementWriter::~ElementWriter() {
  _writer->endElement();
  _resourceStore = nullptr;
}

void ElementWriter::ElementWriter::addFillAndStroke(const FillStyle& fill, const Stroke* stroke,
                                                    const Resources& resources) {
  if (!stroke) {  //fill draw
    static const std::string defaultFill = "black";
    if (resources._paintColor != defaultFill) {
      this->addAttribute("fill", resources._paintColor);
    }
    if (!fill.isOpaque()) {
      this->addAttribute("fill-opacity", fill.color.alpha);
    }
  } else {  //stroke draw
    this->addAttribute("fill", "none");

    this->addAttribute("stroke", resources._paintColor);

    float strokeWidth = stroke->width;
    if (strokeWidth == 0) {
      // Hairline stroke
      strokeWidth = 1;
      this->addAttribute("vector-effect", "non-scaling-stroke");
    }
    this->addAttribute("stroke-width", strokeWidth);

    if (auto cap = ToSVGCap(stroke->cap); !cap.empty()) {
      this->addAttribute("stroke-linecap", cap);
    }

    if (auto join = ToSVGJoin(stroke->join); !join.empty()) {
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
    this->addAttribute("style", ToSVGBlendMode(fill.blendMode));
  }

  if (!resources._filter.empty()) {
    this->addAttribute("filter", resources._filter);
  }
}

void ElementWriter::addAttribute(const std::string& name, const std::string& val) {
  _writer->addAttribute(name, val);
}

void ElementWriter::addAttribute(const std::string& name, int32_t val) {
  _writer->addS32Attribute(name, val);
}

void ElementWriter::addAttribute(const std::string& name, float val) {
  _writer->addScalarAttribute(name, val);
}

void ElementWriter::addText(const std::string& text) {
  _writer->addText(text);
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
                                                const Rect& bound) {
  std::string filterID = _resourceStore->addFilter();
  {
    ImageFilterInfo filterInfo;
    auto filterType = asImageFilterBase(imageFilter)->asImageFilterInfo(&filterInfo);

    ElementWriter filterElement("filter", _context, _writer);
    filterElement.addAttribute("id", filterID);
    filterElement.addAttribute("x", bound.x());
    filterElement.addAttribute("y", bound.y());
    if (filterType == ImageFilterType::InnerShadow) {
      filterElement.addAttribute("width", bound.width() + filterInfo.offset.x);
      filterElement.addAttribute("height", bound.height() + filterInfo.offset.y);
    } else {
      filterElement.addAttribute("width", bound.width());
      filterElement.addAttribute("height", bound.height());
    }
    filterElement.addAttribute("filterUnits", "userSpaceOnUse");

    if (filterType == ImageFilterType::Blur) {
      addBlurImageFilter(filterInfo);
    } else if (filterType == ImageFilterType::DropShadow) {
      addDropShadowImageFilter(filterInfo);
    } else if (filterType == ImageFilterType::InnerShadow) {
      addInnerShadowImageFilter(filterInfo);
    }
  }
  Resources resources;
  resources._filter = "url(#" + filterID + ")";
  return resources;
}

void ElementWriter::addBlurImageFilter(const ImageFilterInfo& info) {
  ElementWriter blurElement("feGaussianBlur", _context, _writer);
  blurElement.addAttribute("stdDeviation", std::max(info.blurrinessX, info.blurrinessY));
  blurElement.addAttribute("result", "blur");
}

void ElementWriter::addDropShadowImageFilter(const ImageFilterInfo& info) {
  {
    ElementWriter offsetElement("feOffset", _context, _writer);
    offsetElement.addAttribute("dx", info.offset.x);
    offsetElement.addAttribute("dy", info.offset.y);
  }
  {
    ElementWriter blurElement("feGaussianBlur", _context, _writer);
    blurElement.addAttribute("stdDeviation", std::max(info.blurrinessX, info.blurrinessY));
    blurElement.addAttribute("result", "blur");
  }
  {
    ElementWriter colorMatrixElement("feColorMatrix", _context, _writer);
    colorMatrixElement.addAttribute("type", "matrix");
    auto color = info.color;
    colorMatrixElement.addAttribute("values", "0 0 0 0 " + FloatToString(color.red) + " 0 0 0 0 " +
                                                  FloatToString(color.green) + " 0 0 0 0 " +
                                                  FloatToString(color.blue) + " 0 0 0 " +
                                                  FloatToString(color.alpha) + " 0");
  }
  if (!info.onlyShadow) {
    ElementWriter blendElement("feBlend", _context, _writer);
    blendElement.addAttribute("mode", "normal");
    blendElement.addAttribute("in", "SourceGraphic");
  }
}
void ElementWriter::addInnerShadowImageFilter(const ImageFilterInfo& info) {
  {
    ElementWriter colorMatrixElement("feColorMatrix", _context, _writer);
    colorMatrixElement.addAttribute("in", "SourceAlpha");
    colorMatrixElement.addAttribute("type", "matrix");
    colorMatrixElement.addAttribute("values", "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 127 0");
    colorMatrixElement.addAttribute("result", "hardAlpha");
  }
  if (!info.onlyShadow) {
    {
      ElementWriter floodElement("feFlood", _context, _writer);
      floodElement.addAttribute("flood-opacity", "0");
      floodElement.addAttribute("result", "BackgroundImageFix");
    }
    {
      ElementWriter blendElement("feBlend", _context, _writer);
      blendElement.addAttribute("mode", "normal");
      blendElement.addAttribute("in", "SourceGraphic");
      blendElement.addAttribute("in2", "BackgroundImageFix");
      blendElement.addAttribute("result", "shape");
    }
  }
  {
    ElementWriter offsetElement("feOffset", _context, _writer);
    offsetElement.addAttribute("dx", info.offset.x);
    offsetElement.addAttribute("dy", info.offset.y);
  }
  {
    ElementWriter blurElement("feGaussianBlur", _context, _writer);
    blurElement.addAttribute("stdDeviation", std::max(info.blurrinessX, info.blurrinessY));
  }
  {
    ElementWriter compositeElement("feComposite", _context, _writer);
    compositeElement.addAttribute("in2", "hardAlpha");
    compositeElement.addAttribute("operator", "arithmetic");
    compositeElement.addAttribute("k2", "-1");
    compositeElement.addAttribute("k3", "1");
  }
  {
    ElementWriter colorMatrixElement("feColorMatrix", _context, _writer);
    colorMatrixElement.addAttribute("type", "matrix");
    auto color = info.color;
    colorMatrixElement.addAttribute("values", "0 0 0 0 " + FloatToString(color.red) + " 0 0 0 0 " +
                                                  FloatToString(color.green) + " 0 0 0 0 " +
                                                  FloatToString(color.blue) + " 0 0 0 " +
                                                  FloatToString(color.alpha) + " 0");
  }
  if (!info.onlyShadow) {
    ElementWriter blendElement("feBlend", _context, _writer);
    blendElement.addAttribute("mode", "normal");
    blendElement.addAttribute("in2", "shape");
  }
}

Resources ElementWriter::addResources(const FillStyle& fill) {
  Resources resources(fill);

  if (auto shader = fill.shader) {
    ElementWriter defs("defs", _context, _writer);
    this->addShaderResources(shader, &resources);
  }

  if (auto colorFilter = fill.colorFilter) {
    // TODO(YGAurora): Implement skia color filters for blend modes other than SrcIn
    BlendMode mode;
    if (colorFilter->asColorMode(nullptr, &mode) && mode == BlendMode::SrcIn) {
      this->addColorFilterResources(*colorFilter, &resources);
    }
  }

  return resources;
}

void ElementWriter::addShaderResources(const std::shared_ptr<Shader>& shader,
                                       Resources* resources) {

  auto shaderType = asShaderBase(shader)->type();
  if (shaderType == ShaderType::Color) {
    this->addColorShaderResources(shader, resources);
  } else if (shaderType == ShaderType::Gradient) {
    this->addGradientShaderResources(shader, resources);
  } else if (shaderType == ShaderType::Image) {
    this->addImageShaderResources(shader, resources);
  }
  // TODO(YGAurora): other shader types
}

void ElementWriter::addColorShaderResources(const std::shared_ptr<Shader>& shader,
                                            Resources* resources) {
  Color color;
  if (shader->asColor(&color)) {
    resources->_paintColor = ToSVGColor(color);
    return;
  }
}

void ElementWriter::addGradientShaderResources(const std::shared_ptr<Shader>& shader,
                                               Resources* resources) {
  GradientInfo info;
  auto type = asShaderBase(shader)->asGradient(&info);

  ASSERT(info.colors.size() == info.positions.size());
  if (type == GradientType::Linear) {
    resources->_paintColor = "url(#" + addLinearGradientDef(info) + ")";
  } else if (type == GradientType::Radial) {
    resources->_paintColor = "url(#" + addRadialGradientDef(info) + ")";
  } else {
    resources->_paintColor = "url(#" + addUnsupportedGradientDef(info) + ")";
  }
}

void ElementWriter::addGradientColors(const GradientInfo& info) {
  ASSERT(info.colors.size() >= 2);
  for (uint32_t i = 0; i < info.colors.size(); ++i) {
    auto color = info.colors[i];
    auto colorStr = ToSVGColor(color);

    ElementWriter stop("stop", _context, _writer);
    stop.addAttribute("offset", info.positions[i]);
    stop.addAttribute("stop-color", colorStr);

    if (!color.isOpaque()) {
      stop.addAttribute("stop-opacity", color.alpha);
    }
  }
}

std::string ElementWriter::addLinearGradientDef(const GradientInfo& info) {
  ASSERT(_resourceStore);
  auto id = _resourceStore->addGradient();

  {
    ElementWriter gradient("linearGradient", _context, _writer);

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
  ASSERT(_resourceStore);
  auto id = _resourceStore->addGradient();

  {
    ElementWriter gradient("radialGradient", _context, _writer);

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
  ASSERT(_resourceStore);
  auto id = _resourceStore->addGradient();

  {
    ElementWriter gradient("linearGradient", _context, _writer);

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

void ElementWriter::addImageShaderResources(const std::shared_ptr<Shader>& shader,
                                            Resources* resources) {
  auto [image, x, y] = asShaderBase(shader)->asImage();
  ASSERT(image);

  auto dataUri = AsDataUri(this->_context, image);
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
  std::string widthValue = transDimension(x, imageWidth);
  std::string heightValue = transDimension(y, imageHeight);

  std::string patternID = _resourceStore->addPattern();
  {
    ElementWriter pattern("pattern", _context, _writer);
    pattern.addAttribute("id", patternID);
    pattern.addAttribute("patternUnits", "userSpaceOnUse");
    pattern.addAttribute("patternContentUnits", "userSpaceOnUse");
    pattern.addAttribute("width", widthValue);
    pattern.addAttribute("height", heightValue);
    pattern.addAttribute("x", 0);
    pattern.addAttribute("y", 0);

    {
      std::string imageID = _resourceStore->addImage();
      ElementWriter imageTag("image", _context, _writer);
      imageTag.addAttribute("id", imageID);
      imageTag.addAttribute("x", 0);
      imageTag.addAttribute("y", 0);
      imageTag.addAttribute("width", image->width());
      imageTag.addAttribute("height", image->height());
      imageTag.addAttribute("xlink:href", static_cast<const char*>(dataUri->data()));
    }
  }
  resources->_paintColor = "url(#" + patternID + ")";
}

void ElementWriter::addColorFilterResources(const ColorFilter& colorFilter, Resources* resources) {
  std::string filterID = _resourceStore->addFilter();
  {
    ElementWriter filterElement("filter", _context, _writer);
    filterElement.addAttribute("id", filterID);
    filterElement.addAttribute("x", "0%");
    filterElement.addAttribute("y", "0%");
    filterElement.addAttribute("width", "100%");
    filterElement.addAttribute("height", "100%");

    Color filterColor;
    BlendMode mode;
    colorFilter.asColorMode(&filterColor, &mode);
    ASSERT(mode == BlendMode::SrcIn);

    {
      // first flood with filter color
      ElementWriter floodElement("feFlood", _context, _writer);
      floodElement.addAttribute("flood-color", ToSVGColor(filterColor));
      floodElement.addAttribute("flood-opacity", filterColor.alpha);
      floodElement.addAttribute("result", "flood");
    }

    {
      // apply the transform to filter color
      ElementWriter compositeElement("feComposite", _context, _writer);
      compositeElement.addAttribute("in", "flood");
      compositeElement.addAttribute("operator", "in");
    }
  }
  resources->_filter = "url(#" + filterID + ")";
}
}  // namespace tgfx