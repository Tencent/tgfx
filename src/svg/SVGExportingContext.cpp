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
#include <_types/_uint32_t.h>
#include <cstdlib>
#include <memory>
#include <string>
#include "ElementWriter.h"
#include "SVGUtils.h"
#include "core/CanvasState.h"
#include "core/FillStyle.h"
#include "core/utils/Caster.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "svg/SVGTextBuilder.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Font.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/PathTypes.h"
#include "tgfx/core/Pixmap.h"
#include "tgfx/core/RRect.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/core/Surface.h"
#include "tgfx/core/TileMode.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/svg/SVGExporter.h"

namespace tgfx {

SVGExportingContext::SVGExportingContext(Context* context, const Rect& viewBox,
                                         std::unique_ptr<XMLWriter> xmlWriter, uint32_t exportFlags)
    : exportFlags(exportFlags), context(context), writer(std::move(xmlWriter)),
      resourceBucket(new ResourceStore) {
  if (viewBox.isEmpty()) {
    return;
  }

  writer->writeHeader();
  // The root <svg> tag gets closed by the destructor.
  rootElement = std::make_unique<ElementWriter>("svg", writer);

  rootElement->addAttribute("xmlns", "http://www.w3.org/2000/svg");
  rootElement->addAttribute("xmlns:xlink", "http://www.w3.org/1999/xlink");
  if (viewBox.x() == 0.0f && viewBox.y() == 0.0f) {
    rootElement->addAttribute("width", viewBox.width());
    rootElement->addAttribute("height", viewBox.height());
  } else {
    std::string viewBoxString = FloatToString(viewBox.x()) + " " + FloatToString(viewBox.y()) +
                                " " + FloatToString(viewBox.width()) + " " +
                                FloatToString(viewBox.height());
    rootElement->addAttribute("viewBox", viewBoxString);
  }
}

void SVGExportingContext::drawRect(const Rect& rect, const MCState& state, const FillStyle& fill) {

  std::unique_ptr<ElementWriter> svg;
  if (RequiresViewportReset(fill)) {
    svg = std::make_unique<ElementWriter>("svg", context, this, writer.get(), resourceBucket.get(),
                                          exportFlags & SVGExportFlags::ConvertTextToPaths, state,
                                          fill);
    svg->addRectAttributes(rect);
  }

  applyClipPath(state.clip);
  ElementWriter rectElement("rect", context, this, writer.get(), resourceBucket.get(),
                            exportFlags & SVGExportFlags::ConvertTextToPaths, state, fill);

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
  applyClipPath(state.clip);
  if (roundRect.isOval()) {
    if (roundRect.rect.width() == roundRect.rect.height()) {
      ElementWriter circleElement("circle", context, this, writer.get(), resourceBucket.get(),
                                  exportFlags & SVGExportFlags::ConvertTextToPaths, state, fill);
      circleElement.addCircleAttributes(roundRect.rect);
      return;
    } else {
      ElementWriter ovalElement("ellipse", context, this, writer.get(), resourceBucket.get(),
                                exportFlags & SVGExportFlags::ConvertTextToPaths, state, fill);
      ovalElement.addEllipseAttributes(roundRect.rect);
    }
  } else {
    ElementWriter rrectElement("rect", context, this, writer.get(), resourceBucket.get(),
                               exportFlags & SVGExportFlags::ConvertTextToPaths, state, fill);
    rrectElement.addRoundRectAttributes(roundRect);
  }
}

void SVGExportingContext::drawShape(std::shared_ptr<Shape> shape, const MCState& state,
                                    const FillStyle& style) {
  applyClipPath(state.clip);
  auto path = shape->getPath();
  ElementWriter pathElement("path", context, this, writer.get(), resourceBucket.get(),
                            exportFlags & SVGExportFlags::ConvertTextToPaths, state, style);
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

  Bitmap bitmap = ImageExportToBitmap(context, image);
  if (!bitmap.isEmpty()) {
    Rect srcRect = Rect::MakeWH(image->width(), image->height());
    float scaleX = rect.width() / srcRect.width();
    float scaleY = rect.height() / srcRect.height();
    float transX = rect.left - srcRect.left * scaleX;
    float transY = rect.top - srcRect.top * scaleY;

    MCState newState;
    newState.matrix = state.matrix;
    newState.matrix.postScale(scaleX, scaleY);
    newState.matrix.postTranslate(transX, transY);

    exportPixmap(Pixmap(bitmap), newState, style);
  }
}

void SVGExportingContext::exportPixmap(const Pixmap& pixmap, const MCState& state,
                                       const FillStyle& style) {
  auto dataUri = AsDataUri(pixmap);
  if (!dataUri) {
    return;
  }

  std::string imageID = resourceBucket->addImage();
  {
    ElementWriter defElement("defs", writer);
    {
      ElementWriter imageElement("image", writer);
      imageElement.addAttribute("id", imageID);
      imageElement.addAttribute("width", pixmap.width());
      imageElement.addAttribute("height", pixmap.height());
      imageElement.addAttribute("xlink:href", static_cast<const char*>(dataUri->data()));
    }
  }
  {
    applyClipPath(state.clip);
    ElementWriter imageUse("use", context, this, writer.get(), resourceBucket.get(),
                           exportFlags & SVGExportFlags::ConvertTextToPaths, state, style);
    imageUse.addAttribute("xlink:href", "#" + imageID);
  }
}

void SVGExportingContext::drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList,
                                           const MCState& state, const FillStyle& style,
                                           const Stroke* stroke) {
  if (!glyphRunList) {
    return;
  }

  bool hasFont = glyphRunList->glyphRuns()[0].glyphFace->asFont(nullptr);

  // If the font needs to be converted to a path but lacks outlines (e.g., emoji font, web font),
  // it cannot be converted.
  applyClipPath(state.clip);
  if (hasFont) {
    if (glyphRunList->hasOutlines() && !glyphRunList->hasColor() &&
        exportFlags & SVGExportFlags::ConvertTextToPaths) {
      exportGlyphsAsPath(glyphRunList, state, style, stroke);
    } else {
      exportGlyphsAsText(glyphRunList, state, style, stroke);
    }
  } else {
    if (glyphRunList->hasColor()) {
      exportGlyphsAsImage(glyphRunList, state, style);
    } else {
      exportGlyphsAsPath(glyphRunList, state, style, stroke);
    }
  }
}

void SVGExportingContext::exportGlyphsAsPath(const std::shared_ptr<GlyphRunList>& glyphRunList,
                                             const MCState& state, const FillStyle& style,
                                             const Stroke* stroke) {
  Path path;
  if (glyphRunList->getPath(&path)) {
    ElementWriter pathElement("path", context, this, writer.get(), resourceBucket.get(),
                              exportFlags & SVGExportFlags::ConvertTextToPaths, state, style,
                              stroke);
    pathElement.addPathAttributes(path, tgfx::SVGExportingContext::PathEncoding());
    if (path.getFillType() == PathFillType::EvenOdd) {
      pathElement.addAttribute("fill-rule", "evenodd");
    }
  }
}

void SVGExportingContext::exportGlyphsAsText(const std::shared_ptr<GlyphRunList>& glyphRunList,
                                             const MCState& state, const FillStyle& style,
                                             const Stroke* stroke) {
  for (const auto& glyphRun : glyphRunList->glyphRuns()) {
    ElementWriter textElement("text", context, this, writer.get(), resourceBucket.get(),
                              exportFlags & SVGExportFlags::ConvertTextToPaths, state, style,
                              stroke);

    Font font;
    if (glyphRun.glyphFace->asFont(&font)) {
      textElement.addFontAttributes(font);

      auto unicharInfo = textBuilder.glyphToUnicharsInfo(glyphRun);
      textElement.addAttribute("x", unicharInfo.posX);
      textElement.addAttribute("y", unicharInfo.posY);
      textElement.addText(unicharInfo.text);
    }
  }
}

void SVGExportingContext::exportGlyphsAsImage(const std::shared_ptr<GlyphRunList>& glyphRunList,
                                              const MCState& state, const FillStyle& style) {
  auto viewMatrix = state.matrix;
  auto scale = viewMatrix.getMaxScale();
  if (scale <= 0) {
    return;
  }
  viewMatrix.preScale(1.0f / scale, 1.0f / scale);
  for (const auto& glyphRun : glyphRunList->glyphRuns()) {
    auto glyphFace = glyphRun.glyphFace;
    glyphFace = glyphFace->makeScaled(scale);
    if (glyphFace == nullptr) {
      continue;
    }
    const auto& glyphIDs = glyphRun.glyphs;
    auto glyphCount = glyphIDs.size();
    const auto& positions = glyphRun.positions;
    auto glyphState = state;
    for (size_t i = 0; i < glyphCount; ++i) {
      const auto& glyphID = glyphIDs[i];
      const auto& position = positions[i];
      auto glyphImage = glyphFace->getImage(glyphID, &glyphState.matrix);
      if (glyphImage == nullptr) {
        continue;
      }
      glyphState.matrix.postTranslate(position.x * scale, position.y * scale);
      glyphState.matrix.postConcat(viewMatrix);
      auto rect = Rect::MakeWH(glyphImage->width(), glyphImage->height());
      drawImageRect(std::move(glyphImage), rect, {}, glyphState, style);
    }
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
    applyClipPath(state.clip);
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

  if (const auto* imageShader = ShaderCaster::AsImageShader(shader.get())) {
    return imageShader->tileModeX == TileMode::Repeat || imageShader->tileModeY == TileMode::Repeat;
  }
  return false;
}

PathEncoding SVGExportingContext::PathEncoding() {
  return PathEncoding::Absolute;
}

void SVGExportingContext::applyClipPath(const Path& clipPath) {
  auto defineClip = [this](const Path& clipPath) -> std::string {
    std::string clipID = resourceBucket->addClip();
    ElementWriter clipPathElement("clipPath", writer);
    clipPathElement.addAttribute("id", clipID);
    {
      std::unique_ptr<ElementWriter> element;
      Rect rect;
      RRect rrect;
      Rect ovalBound;
      if (clipPath.isRect(&rect)) {
        element = std::make_unique<ElementWriter>("rect", writer);
        element->addRectAttributes(rect);
      } else if (clipPath.isRRect(&rrect)) {
        element = std::make_unique<ElementWriter>("rect", writer);
        element->addRoundRectAttributes(rrect);
      } else if (clipPath.isOval(&ovalBound)) {
        if (FloatNearlyEqual(ovalBound.width(), ovalBound.height())) {
          element = std::make_unique<ElementWriter>("circle", writer);
          element->addCircleAttributes(ovalBound);
        } else {
          element = std::make_unique<ElementWriter>("ellipse", writer);
          element->addEllipseAttributes(ovalBound);
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

  if (clipPath == currentClipPath) {
    return;
  }
  clipGroupElement = nullptr;
  if (clipPath.isEmpty()) {
    return;
  }
  currentClipPath = clipPath;
  auto clipID = defineClip(currentClipPath);
  clipGroupElement = std::make_unique<ElementWriter>("g", writer);
  clipGroupElement->addAttribute("clip-path", "url(#" + clipID + ")");
}

Bitmap SVGExportingContext::ImageExportToBitmap(Context* context,
                                                const std::shared_ptr<Image>& image) {
  DEBUG_ASSERT(context);
  auto surface = Surface::Make(context, image->width(), image->height());
  auto* canvas = surface->getCanvas();
  canvas->drawImage(image);

  Bitmap bitmap(image->width(), image->height(), false, false);
  Pixmap pixmap(bitmap);
  if (surface->readPixels(pixmap.info(), pixmap.writablePixels())) {
    return bitmap;
  }
  return Bitmap();
}

std::shared_ptr<Data> SVGExportingContext::ImageToEncodedData(const std::shared_ptr<Image>& image) {
  const auto* generatorImage = ImageCaster::AsGeneratorImage(image.get());
  if (!generatorImage) {
    return nullptr;
  }

  auto generator = generatorImage->generator;
  if (!generator) {
    return nullptr;
  }
  const auto* imageCodec = ImageGeneratorCaster::AsImageCodec(generator.get());
  if (!imageCodec) {
    return nullptr;
  }
  return imageCodec->encodedData();
}

}  // namespace tgfx