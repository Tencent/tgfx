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
#include <_types/_uint32_t.h>
#include <string>
#include <unordered_set>
#include "SVGExportContext.h"
#include "SVGUtils.h"
#include "core/CanvasState.h"
#include "core/codecs/jpeg/JpegCodec.h"
#include "core/codecs/png/PngCodec.h"
#include "core/filters/ShaderMaskFilter.h"
#include "core/utils/Caster.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/GradientType.h"
#include "tgfx/core/Pixmap.h"
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
                             SVGExportContext* svgContext, XMLWriter* writer, ResourceStore* bucket,
                             bool disableWarning, const MCState& state, const FillStyle& fill,
                             const Stroke* stroke)
    : writer(writer), resourceStore(bucket), disableWarning(disableWarning) {
  Resources resource = addResources(fill, context, svgContext);

  writer->startElement(name);

  addFillAndStroke(fill, stroke, resource);

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

void ElementWriter::addFillAndStroke(const FillStyle& fill, const Stroke* stroke,
                                     const Resources& resources) {
  if (!stroke) {  //fill draw
    static const std::string defaultFill = "black";
    if (resources.paintColor != defaultFill) {
      addAttribute("fill", resources.paintColor);
    }
    if (!fill.isOpaque()) {
      addAttribute("fill-opacity", fill.color.alpha);
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

    if (!fill.isOpaque()) {
      addAttribute("stroke-opacity", fill.color.alpha);
    }
  }

  if (fill.blendMode != BlendMode::SrcOver) {
    auto blendModeString = ToSVGBlendMode(fill.blendMode);
    if (!blendModeString.empty()) {
      addAttribute("style", blendModeString);
    } else {
      reportUnsupportedElement("Unsupported blend mode");
    }
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

void ElementWriter::addPathAttributes(const Path& path, PathEncoding encoding) {
  addAttribute("d", ToSVGPath(path, encoding));
}

Resources ElementWriter::addImageFilterResource(const std::shared_ptr<ImageFilter>& imageFilter,
                                                Rect bound) {
  std::string filterID = resourceStore->addFilter();
  {
    ElementWriter filterElement("filter", writer);
    filterElement.addAttribute("id", filterID);
    if (const auto* blurFilter = Caster::AsBlurImageFilter(imageFilter.get())) {
      bound = blurFilter->filterBounds(bound);
      filterElement.addAttribute("x", bound.x());
      filterElement.addAttribute("y", bound.y());
      filterElement.addAttribute("width", bound.width());
      filterElement.addAttribute("height", bound.height());
      filterElement.addAttribute("filterUnits", "userSpaceOnUse");
      addBlurImageFilter(blurFilter);
    } else if (const auto* dropShadowFilter = Caster::AsDropShadowImageFilter(imageFilter.get())) {
      bound = blurFilter->filterBounds(bound);
      filterElement.addAttribute("x", bound.x());
      filterElement.addAttribute("y", bound.y());
      filterElement.addAttribute("width", bound.width());
      filterElement.addAttribute("height", bound.height());
      filterElement.addAttribute("filterUnits", "userSpaceOnUse");
      addDropShadowImageFilter(dropShadowFilter);
    } else if (const auto* innerShadowFilter =
                   Caster::AsInnerShadowImageFilter(imageFilter.get())) {
      bound = blurFilter->filterBounds(bound);
      filterElement.addAttribute("x", bound.x());
      filterElement.addAttribute("y", bound.y());
      filterElement.addAttribute("width", bound.width() + innerShadowFilter->dx);
      filterElement.addAttribute("height", bound.height() + innerShadowFilter->dy);
      filterElement.addAttribute("filterUnits", "userSpaceOnUse");
      addInnerShadowImageFilter(innerShadowFilter);
    } else {
      // TODO (YGaurora): The compose filter can be expanded into multiple filters for export. This
      // can be implemented.
      reportUnsupportedElement("Unsupported image filter");
    }
  }
  Resources resources;
  resources.filter = "url(#" + filterID + ")";
  return resources;
}

void ElementWriter::addBlurImageFilter(const BlurImageFilter* filter) {
  ElementWriter blurElement("feGaussianBlur", writer);
  auto blurSize = filter->filterBounds(Rect::MakeEmpty()).size();
  blurElement.addAttribute("stdDeviation", std::max(blurSize.width / 4.f, blurSize.height / 4.f));
  blurElement.addAttribute("result", "blur");
}

void ElementWriter::addDropShadowImageFilter(const DropShadowImageFilter* filter) {
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

Resources ElementWriter::addResources(const FillStyle& fill, Context* context,
                                      SVGExportContext* svgContext) {
  Resources resources(fill);

  if (auto shader = fill.shader) {
    ElementWriter defs("defs", writer);
    addShaderResources(shader, context, &resources);
  }

  if (auto colorFilter = fill.colorFilter) {
    if (const auto* blendFilter = Caster::AsModeColorFilter(colorFilter.get())) {
      addBlendColorFilterResources(blendFilter, &resources);
    } else if (const auto* matrixFilter = Caster::AsMatrixColorFilter(colorFilter.get())) {
      addMatrixColorFilterResources(matrixFilter, &resources);
    } else {
      reportUnsupportedElement("Unsupported color filter");
    }
  }

  if (auto maskFilter = fill.maskFilter) {
    addMaskResources(maskFilter, &resources, context, svgContext);
  }

  return resources;
}

void ElementWriter::addShaderResources(const std::shared_ptr<Shader>& shader, Context* context,
                                       Resources* resources) {
  if (const auto* colorShader = Caster::AsColorShader(shader.get())) {
    addColorShaderResources(colorShader, resources);
  } else if (const auto* gradientShader = Caster::AsGradientShader(shader.get())) {
    addGradientShaderResources(gradientShader, resources);
  } else if (const auto* imageShader = Caster::AsImageShader(shader.get())) {
    addImageShaderResources(imageShader, context, resources);
  } else {
    // TODO(YGaurora):
    // Export color filter shaders as color filters.
    // Export blend shaders as a combination of a shader and blend mode.
    // Export matrix shaders as a combination of a shader and matrix. The SVG standard allows
    // writing the matrix into <pattern> using patternTransform.
    reportUnsupportedElement("Unsupported shader");
  }
}

void ElementWriter::addColorShaderResources(const ColorShader* shader, Resources* resources) {
  Color color;
  if (shader->asColor(&color)) {
    resources->paintColor = ToSVGColor(color);
  }
}

void ElementWriter::addGradientShaderResources(const GradientShader* shader, Resources* resources) {
  GradientInfo info;
  GradientType type = shader->asGradient(&info);

  DEBUG_ASSERT(info.colors.size() == info.positions.size());
  if (type == GradientType::Linear) {
    resources->paintColor = "url(#" + addLinearGradientDef(info) + ")";
  } else if (type == GradientType::Radial) {
    resources->paintColor = "url(#" + addRadialGradientDef(info) + ")";
  } else {
    resources->paintColor = "url(#" + addUnsupportedGradientDef(info) + ")";
    reportUnsupportedElement("Unsupported gradient type");
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

void ElementWriter::addImageShaderResources(const ImageShader* shader, Context* context,
                                            Resources* resources) {
  auto image = shader->image;
  DEBUG_ASSERT(image);

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
      floodElement.addAttribute("flood-color", ToSVGColor(modeColorFilter->color));
      floodElement.addAttribute("flood-opacity", modeColorFilter->color.alpha);
      floodElement.addAttribute("result", "flood");
    }

    {
      ElementWriter blendElement("feBlend", writer);
      blendElement.addAttribute("in", "SourceGraphic");
      blendElement.addAttribute("in2", "flood");
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

  const auto* maskShaderFilter = Caster::AsShaderMaskFilter(maskFilter.get());
  if (!maskShaderFilter) {
    return;
  }

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
  if (const auto* imageShader = Caster::AsImageShader(maskShader.get())) {
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
  } else if (Caster::AsColorShader(maskShader.get()) ||
             Caster::AsGradientShader(maskShader.get())) {
    ElementWriter maskElement("mask", writer);
    auto maskID = resourceStore->addMask();
    maskElement.addAttribute("id", maskID);
    maskElement.addAttribute("style", "mask-type:alpha");
    maskElement.addAttribute("maskUnits", "userSpaceOnUse");
    maskElement.addAttribute("width", "100%");
    maskElement.addAttribute("height", "100%");

    addShaderMaskResources(maskShader, filterID, context);

    resources->mask = "url(#" + maskID + ")";
  } else {
    // TODO (YGaurora): The mask filter can be expanded to support shaders. Once shaders are
    // supported, the corresponding mask filter will also be supported.
    reportUnsupportedElement("unsupported mask filter");
  }
}

void ElementWriter::addImageMaskResources(const ImageShader* imageShader,
                                          const std::string& filterID, Context* context,
                                          SVGExportContext* svgContext) {
  auto image = imageShader->image;
  if (const auto* pictureImage = Caster::AsPictureImage(image.get())) {
    addPictureImageMaskResources(pictureImage, filterID, svgContext);
  } else {
    addRenderImageMaskResources(imageShader, filterID, context);
  }
}

void ElementWriter::addPictureImageMaskResources(const PictureImage* pictureImage,
                                                 const std::string& filterID,
                                                 SVGExportContext* svgContext) {
  auto picture = pictureImage->picture;
  auto pictureBound = picture->getBounds(pictureImage->matrix);
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
  addImageShaderResources(imageShader, context, &resources);

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
  if (!filterID.empty()) {
    writer->addAttribute("filter", filterID);
  }
  addAttribute("width", "100%");
  addAttribute("height", "100%");
  writer->endElement();
}

}  // namespace tgfx