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

#include "SVGExportingContext.h"
#include <cstdlib>
#include <memory>
#include <string>
#include "ElementWriter.h"
#include "SVGUtils.h"
#include "core/FillStyle.h"
#include "core/MCState.h"
#include "core/utils/Caster.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "svg/SVGTextBuilder.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/PathTypes.h"
#include "tgfx/core/RRect.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/core/Surface.h"
#include "tgfx/core/TileMode.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/svg/SVGExporter.h"

namespace tgfx {

SVGExportingContext::SVGExportingContext(Context* gpuContext, const ISize& size,
                                         std::unique_ptr<XMLWriter> xmlWriter, uint32_t options)
    : options(options), size(size), context(gpuContext), writer(std::move(xmlWriter)),
      resourceBucket(new ResourceStore) {
  ASSERT(writer);
  writer->writeHeader();

  if (size.width == 0 || size.height == 0) {
    return;
  }

  // The root <svg> tag gets closed by the destructor.
  rootElement = std::make_unique<ElementWriter>("svg", writer);

  rootElement->addAttribute("xmlns", "http://www.w3.org/2000/svg");
  rootElement->addAttribute("xmlns:xlink", "http://www.w3.org/1999/xlink");
  rootElement->addAttribute("width", size.width);
  rootElement->addAttribute("height", size.height);
}

void SVGExportingContext::clear() {
  rootElement = nullptr;
  writer->clear();
  writer->writeHeader();

  rootElement = std::make_unique<ElementWriter>("svg", writer);
  rootElement->addAttribute("xmlns", "http://www.w3.org/2000/svg");
  rootElement->addAttribute("xmlns:xlink", "http://www.w3.org/1999/xlink");
  rootElement->addAttribute("width", size.width);
  rootElement->addAttribute("height", size.height);
};

void SVGExportingContext::drawRect(const Rect& rect, const MCState& mc, const FillStyle& fill) {

  std::unique_ptr<ElementWriter> svg;
  if (RequiresViewportReset(fill)) {
    svg = std::make_unique<ElementWriter>("svg", context, this, resourceBucket.get(), mc, fill);
    svg->addRectAttributes(rect);
  }

  ElementWriter rectElement("rect", context, this, resourceBucket.get(), mc, fill);

  if (svg) {
    rectElement.addAttribute("x", 0);
    rectElement.addAttribute("y", 0);
    rectElement.addAttribute("width", "100%");
    rectElement.addAttribute("height", "100%");
  } else {
    rectElement.addRectAttributes(rect);
  }
}

void SVGExportingContext::drawRRect(const RRect& roundRect, const MCState& state,
                                    const FillStyle& fill) {
  if (roundRect.isOval()) {
    if (roundRect.rect.width() == roundRect.rect.height()) {
      ElementWriter circleElement("circle", context, this, resourceBucket.get(), state, fill);
      circleElement.addCircleAttributes(roundRect.rect);
      return;
    } else {
      ElementWriter ovalElement("ellipse", context, this, resourceBucket.get(), state, fill);
      ovalElement.addEllipseAttributes(roundRect.rect);
    }
  } else {
    ElementWriter rrectElement("rect", context, this, resourceBucket.get(), state, fill);
    rrectElement.addRoundRectAttributes(roundRect);
  }
}

void SVGExportingContext::drawShape(std::shared_ptr<Shape> shape, const MCState& state,
                                    const FillStyle& style) {
  auto path = shape->getPath();
  ElementWriter pathElement("path", context, this, resourceBucket.get(), state, style);
  pathElement.addPathAttributes(path, tgfx::SVGExportingContext::PathEncoding());
  if (path.getFillType() == PathFillType::EvenOdd) {
    pathElement.addAttribute("fill-rule", "evenodd");
  }
}

void SVGExportingContext::drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                                    const MCState& state, const FillStyle& style) {
  if (image == nullptr) {
    return;
  }
  auto rect = Rect::MakeWH(image->width(), image->height());
  return drawImageRect(std::move(image), rect, sampling, state, style);
}

void SVGExportingContext::drawImageRect(std::shared_ptr<Image> image, const Rect& rect,
                                        const SamplingOptions&, const MCState& state,
                                        const FillStyle& style) {
  if (image == nullptr) {
    return;
  }

  auto bitmap = ImageToBitmap(this->context, image);

  Rect srcRect = Rect::MakeWH(image->width(), image->height());
  float scaleX = rect.width() / srcRect.width();
  float scaleY = rect.height() / srcRect.height();
  float transX = rect.left - srcRect.left * scaleX;
  float transY = rect.top - srcRect.top * scaleY;

  MCState newState;
  newState.matrix = state.matrix;
  newState.matrix.postScale(scaleX, scaleY);
  newState.matrix.postTranslate(transX, transY);

  drawBitmap(bitmap, newState, style);
}

void SVGExportingContext::drawBitmap(const Bitmap& bitmap, const MCState& state,
                                     const FillStyle& style) {
  auto dataUri = AsDataUri(bitmap);
  if (!dataUri) {
    return;
  }

  std::string imageID = resourceBucket->addImage();
  {
    ElementWriter defElement("defs", writer);
    {
      ElementWriter imageElement("image", writer);
      imageElement.addAttribute("id", imageID);
      imageElement.addAttribute("width", bitmap.width());
      imageElement.addAttribute("height", bitmap.height());
      imageElement.addAttribute("xlink:href", static_cast<const char*>(dataUri->data()));
    }
  }
  {
    ElementWriter imageUse("use", context, this, resourceBucket.get(), state, style);
    imageUse.addAttribute("xlink:href", "#" + imageID);
  }
}

void SVGExportingContext::drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList,
                                           const MCState& state, const FillStyle& style,
                                           const Stroke* stroke) {
  if (!glyphRunList) {
    return;
  }

  // If the font has color (emoji) or does not have outlines (web font), it cannot be converted to
  // a path.
  // If the options do not require converting text to paths and there is no stroke, do not convert
  // to a path.
  if (glyphRunList->hasColor() || !glyphRunList->hasOutlines() ||
      (!(options & SVGExporter::ConvertTextToPaths) && stroke == nullptr)) {
    exportGlyphsAsText(glyphRunList, state, style);
  } else {
    // If the options require converting text to paths or there is a stroke, convert to a path.
    exportGlyphsAsPath(std::move(glyphRunList), state, style, stroke);
  }
}

void SVGExportingContext::exportGlyphsAsPath(const std::shared_ptr<GlyphRunList>& glyphRunList,
                                             const MCState& state, const FillStyle& style,
                                             const Stroke* stroke) {
  if (Path path; glyphRunList->getPath(&path)) {
    ElementWriter pathElement("path", context, this, resourceBucket.get(), state, style, stroke);
    pathElement.addPathAttributes(path, tgfx::SVGExportingContext::PathEncoding());
    if (path.getFillType() == PathFillType::EvenOdd) {
      pathElement.addAttribute("fill-rule", "evenodd");
    }
  }
}

void SVGExportingContext::exportGlyphsAsText(const std::shared_ptr<GlyphRunList>& glyphRunList,
                                             const MCState& state, const FillStyle& style) {
  for (const auto& glyphRun : glyphRunList->glyphRuns()) {
    ElementWriter textElement("text", context, this, resourceBucket.get(), state, style);
    textElement.addFontAttributes(glyphRun.font);

    SVGTextBuilder builder(glyphRun);
    textElement.addAttribute("x", builder.posX());
    textElement.addAttribute("y", builder.posY());
    textElement.addText(builder.text());
  }
}

void SVGExportingContext::drawPicture(std::shared_ptr<Picture> picture, const MCState& state) {
  if (picture != nullptr) {
    picture->playback(this, state);
  }
}

void SVGExportingContext::drawLayer(std::shared_ptr<Picture> picture, const MCState& state,
                                    const FillStyle&, std::shared_ptr<ImageFilter> imageFilter) {
  if (picture == nullptr) {
    return;
  }

  Resources resources;
  if (imageFilter) {
    ElementWriter defs("defs", writer, resourceBucket.get());
    auto bound = picture->getBounds();
    resources = defs.addImageFilterResource(imageFilter, bound);
  }
  {
    auto groupElement = std::make_unique<ElementWriter>("g", writer, resourceBucket.get());
    if (imageFilter) {
      groupElement->addAttribute("filter", resources.filter);
    }
    picture->playback(this, state);
  }
}

bool SVGExportingContext::RequiresViewportReset(const FillStyle& fill) {
  auto shader = fill.shader;
  if (!shader) {
    return false;
  }

  if (auto imageShader = ShaderCaster::AsImageShader(shader)) {
    return imageShader->tileModeX == TileMode::Repeat || imageShader->tileModeY == TileMode::Repeat;
  }
  return false;
}

PathEncoding SVGExportingContext::PathEncoding() {
  return PathEncoding::Absolute;
}

void SVGExportingContext::syncMCState(const MCState& state) {
  auto saveCount = canvas->getSaveCount();
  if (!stateStack.empty()) {
    if (stateStack.top().first >= saveCount) {
      while (stateStack.top().first > saveCount) {
        stateStack.pop();
      }
      return;
    }
  }

  if (state.clip.isEmpty()) {
    return;
  }

  auto defineClip = [this](const MCState& insertState) -> std::string {
    std::string clipID = "clip_" + resourceBucket->addClip();
    ElementWriter clipPath("clipPath", writer);
    clipPath.addAttribute("id", clipID);
    {
      auto clipPath = insertState.clip;
      std::unique_ptr<ElementWriter> element;
      if (Rect rect; clipPath.isRect(&rect)) {
        element = std::make_unique<ElementWriter>("rect", writer);
        element->addRectAttributes(rect);
      } else if (RRect rrect; clipPath.isRRect(&rrect)) {
        element = std::make_unique<ElementWriter>("rect", writer);
        element->addRoundRectAttributes(rrect);
      } else if (Rect bound; clipPath.isOval(&bound)) {
        if (FloatNearlyEqual(bound.width(), bound.height())) {
          element = std::make_unique<ElementWriter>("circle", writer);
          element->addCircleAttributes(bound);
        } else {
          element = std::make_unique<ElementWriter>("ellipse", writer);
          element->addEllipseAttributes(bound);
        }
      } else {
        element = std::make_unique<ElementWriter>("path", writer);
        element->addPathAttributes(clipPath, tgfx::SVGExportingContext::PathEncoding());
        if (clipPath.getFillType() == PathFillType::EvenOdd) {
          element->addAttribute("clip-rule", "evenodd");
        }
      }
    }
    return clipID;
  };

  auto clipID = defineClip(state);
  auto clipElement = std::make_unique<ElementWriter>("g", writer);
  clipElement->addAttribute("clip-path", "url(#" + clipID + ")");
  stateStack.emplace(saveCount, std::move(clipElement));
}

}  // namespace tgfx