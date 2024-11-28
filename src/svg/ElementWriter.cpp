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
#include <unordered_set>
#include "SVGContext.h"
#include "SVGUtils.h"
#include "core/MCState.h"
#include "core/shaders/ShaderBase.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Surface.h"

namespace tgfx {

Resources::Resources(const FillStyle& fill) {
  _paintServer = SVGColor(fill.color);
}

ElementWriter::ElementWriter(const std::string& name, Context* GPUContext, XMLWriter* writer)
    : _context(GPUContext), _writer(writer) {
  _writer->startElement(name);
}

ElementWriter::ElementWriter(const std::string& name, Context* GPUContext,
                             const std::unique_ptr<XMLWriter>& writer)
    : ElementWriter(name, GPUContext, writer.get()) {
}

ElementWriter::ElementWriter(const std::string& name, Context* GPUContext, SVGContext* svgContext,
                             ResourceStore* bucket, const MCState& state, const FillStyle& fill,
                             const Stroke* stroke)
    : _context(GPUContext), _writer(svgContext->getWriter()), _resourceStore(bucket) {

  svgContext->syncClipStack(state);
  Resources res = this->addResources(fill);

  _writer->startElement(name);

  this->addFillAndStroke(fill, stroke, res);

  if (!state.matrix.isIdentity()) {
    this->addAttribute("transform", SVGTransform(state.matrix));
  }
}

ElementWriter::~ElementWriter() {
  _writer->endElement();
  /////////////////////////////////////////////////
  _resourceStore = nullptr;
}

void ElementWriter::ElementWriter::addFillAndStroke(const FillStyle& fill, const Stroke* stroke,
                                                    const Resources& resources) {
  if (!stroke) {  //fill draw
    static const std::string defaultFill = "black";
    if (resources._paintServer != defaultFill) {
      this->addAttribute("fill", resources._paintServer);
    }
    if (!fill.isOpaque()) {
      this->addAttribute("fill-opacity", fill.color.alpha);
    }
  } else {  //stroke draw
    this->addAttribute("fill", "none");

    this->addAttribute("stroke", resources._paintServer);

    float strokeWidth = stroke->width;
    if (strokeWidth == 0) {
      // Hairline stroke
      strokeWidth = 1;
      this->addAttribute("vector-effect", "non-scaling-stroke");
    }
    this->addAttribute("stroke-width", strokeWidth);

    if (auto cap = SVGCap(stroke->cap); !cap.empty()) {
      this->addAttribute("stroke-linecap", cap);
    }

    if (auto join = SVGJoin(stroke->join); !join.empty()) {
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
    this->addAttribute("style", SVGBlendMode(fill.blendMode));
  }

  if (!resources._colorFilter.empty()) {
    this->addAttribute("filter", resources._colorFilter);
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

void ElementWriter::addPathAttributes(const Path& path, PathParse::PathEncoding encoding) {
  this->addAttribute("d", PathParse::ToSVGString(path, encoding));
}

void ElementWriter::addTextAttributes(const Font& font) {
  //TODO(YGAurora): add font attributes
  this->addAttribute("font-size", font.getSize());

  std::string familyName;
  std::unordered_set<std::string> familySet;
  auto typeFace = font.getTypeface();

  ASSERT(typeFace);
  // auto style = typeFace->fontStyle();
  this->addAttribute("font-style", font.isFauxItalic() ? "italic" : "oblique");

  this->addAttribute("font-weight", font.isFauxBold() ? "normal" : "bold");
  // int weightIndex = (SkTPin(style.weight(), 100, 900) - 50) / 100;
  // if (weightIndex != 3) {
  //   static constexpr const char* weights[] = {"100", "200", "300",  "normal", "400",
  //                                             "500", "600", "bold", "800",    "900"};
  //   this->addAttribute("font-weight", weights[weightIndex]);
  // }

  // int stretchIndex = style.width() - 1;
  // if (stretchIndex != 4) {
  //   static constexpr const char* stretches[] = {
  //       "ultra-condensed", "extra-condensed", "condensed",      "semi-condensed", "normal",
  //       "semi-expanded",   "expanded",        "extra-expanded", "ultra-expanded"};
  //   this->addAttribute("font-stretch", stretches[stretchIndex]);
  // }

  // sk_sp<SkTypeface::LocalizedStrings> familyNameIter(tface->createFamilyNameIterator());
  // SkTypeface::LocalizedString familyString;
  // if (familyNameIter) {
  //   while (familyNameIter->next(&familyString)) {
  //     if (familySet.contains(familyString.fString)) {
  //       continue;
  //     }
  //     familySet.add(familyString.fString);
  //     familyName.appendf((familyName.isEmpty() ? "%s" : ", %s"), familyString.fString.c_str());
  //   }
  // }
  // if (!familyName.isEmpty()) {
  //   this->addAttribute("font-family", familyName);
  // }
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
  if (shaderType == ShaderType::Color || shaderType == ShaderType::Gradient) {
    this->addGradientShaderResources(shader, resources);
  } else if (shaderType == ShaderType::Image) {
    this->addImageShaderResources(shader, resources);
  }
  // TODO(YGAurora): other shader types?
}

void ElementWriter::addGradientShaderResources(const std::shared_ptr<Shader>& shader,
                                               Resources* resources) {
  if (asShaderBase(shader)->type() == ShaderType::Color) {
    Color color;
    if (shader->asColor(&color)) {
      resources->_paintServer = SVGColor(color);
      return;
    }
  }

  GradientInfo info;
  auto type = asShaderBase(shader)->asGradient(&info);

  ASSERT(info.colors.size() == info.positions.size());
  if (type == GradientType::Linear) {
    resources->_paintServer = "url(#" + addLinearGradientDef(info) + ")";
  } else if (type == GradientType::Radial) {
    resources->_paintServer = "url(#" + addRadialGradientDef(info) + ")";
  } else {
    resources->_paintServer = "url(#" + addUnsupportedGradientDef(info) + ")";
  }
}

void ElementWriter::addGradientColors(const GradientInfo& info) {
  ASSERT(info.colors.size() >= 2);
  for (uint32_t i = 0; i < info.colors.size(); ++i) {
    auto color = info.colors[i];
    auto colorStr = SVGColor(color);

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
  resources->_paintServer = "url(#" + patternID + ")";
}

void ElementWriter::addColorFilterResources(const ColorFilter& colorFilter, Resources* resources) {
  std::string colorFilterID = _resourceStore->addColorFilter();
  {
    ElementWriter filterElement("filter", _context, _writer);
    filterElement.addAttribute("id", colorFilterID);
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
      floodElement.addAttribute("flood-color", SVGColor(filterColor));
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
  resources->_colorFilter = "url(#" + colorFilterID + ")";
}
}  // namespace tgfx