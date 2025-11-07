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

#include "SVGExportContext.h"
#include "ElementWriter.h"
#include "SVGUtils.h"
#include "core/images/CodecImage.h"
#include "core/images/FilterImage.h"
#include "core/images/PictureImage.h"
#include "core/images/SubsetImage.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "core/utils/RectToRectMatrix.h"
#include "core/utils/ShapeUtils.h"
#include "core/utils/Types.h"
#include "svg/SVGTextBuilder.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Fill.h"
#include "tgfx/core/Font.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/PathTypes.h"
#include "tgfx/core/Pixmap.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/RRect.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/core/Surface.h"
#include "tgfx/core/TileMode.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/svg/SVGExporter.h"
#include "tgfx/svg/SVGPathParser.h"

namespace tgfx {

SVGExportContext::SVGExportContext(Context* context, const Rect& viewBox,
                                   std::unique_ptr<XMLWriter> xmlWriter, uint32_t exportFlags)
    : exportFlags(exportFlags), context(context), viewBox(viewBox), writer(std::move(xmlWriter)),
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

void SVGExportContext::drawFill(const Fill& fill) {
  drawRect(viewBox, {}, fill, nullptr);
}

void SVGExportContext::drawRect(const Rect& rect, const MCState& state, const Fill& fill,
                                const Stroke*) {

  std::unique_ptr<ElementWriter> svg;
  if (RequiresViewportReset(fill)) {
    svg =
        std::make_unique<ElementWriter>("svg", context, this, writer.get(), resourceBucket.get(),
                                        exportFlags & SVGExportFlags::DisableWarnings, state, fill);
    svg->addRectAttributes(rect);
  }

  if (!state.clip.contains(rect)) {
    applyClipPath(state.clip);
  }

  ElementWriter rectElement("rect", context, this, writer.get(), resourceBucket.get(),
                            exportFlags & SVGExportFlags::DisableWarnings, state, fill);

  if (svg) {
    rectElement.addAttribute("x", 0);
    rectElement.addAttribute("y", 0);
    rectElement.addAttribute("width", "100%");
    rectElement.addAttribute("height", "100%");
  } else {
    rectElement.addRectAttributes(rect);
  }
}

void SVGExportContext::drawRRect(const RRect& roundRect, const MCState& state, const Fill& fill,
                                 const Stroke*) {
  if (!state.clip.contains(roundRect.rect)) {
    applyClipPath(state.clip);
  }
  if (roundRect.isOval()) {
    if (roundRect.rect.width() == roundRect.rect.height()) {
      ElementWriter circleElement("circle", context, this, writer.get(), resourceBucket.get(),
                                  exportFlags & SVGExportFlags::DisableWarnings, state, fill);
      circleElement.addCircleAttributes(roundRect.rect);
      return;
    } else {
      ElementWriter ovalElement("ellipse", context, this, writer.get(), resourceBucket.get(),
                                exportFlags & SVGExportFlags::DisableWarnings, state, fill);
      ovalElement.addEllipseAttributes(roundRect.rect);
    }
  } else {
    ElementWriter rrectElement("rect", context, this, writer.get(), resourceBucket.get(),
                               exportFlags & SVGExportFlags::DisableWarnings, state, fill);
    rrectElement.addRoundRectAttributes(roundRect);
  }
}

void SVGExportContext::drawPath(const Path& path, const MCState& state, const Fill& fill) {
  if (!state.clip.contains(path.getBounds())) {
    applyClipPath(state.clip);
  }
  ElementWriter pathElement("path", context, this, writer.get(), resourceBucket.get(),
                            exportFlags & SVGExportFlags::DisableWarnings, state, fill);
  pathElement.addPathAttributes(path, tgfx::SVGExportContext::PathEncodingType());
  if (path.getFillType() == PathFillType::EvenOdd) {
    pathElement.addAttribute("fill-rule", "evenodd");
  }
}

void SVGExportContext::drawShape(std::shared_ptr<Shape> shape, const MCState& state,
                                 const Fill& fill, const Stroke* stroke) {
  DEBUG_ASSERT(shape != nullptr);
  shape = Shape::ApplyStroke(shape, stroke);
  auto path = ShapeUtils::GetShapeRenderingPath(shape, state.matrix.getMaxScale());
  drawPath(path, state, fill);
}

void SVGExportContext::drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                                 const MCState& state, const Fill& fill) {
  DEBUG_ASSERT(image != nullptr);
  auto type = Types::Get(image.get());
  if (type == Types::ImageType::Picture) {
    const auto pictureImage = static_cast<const PictureImage*>(image.get());
    auto newState = state;
    if (pictureImage->matrix) {
      newState.matrix.preConcat(*pictureImage->matrix);
    }
    drawPicture(pictureImage->picture, newState);
  } else if (type == Types::ImageType::Filter) {
    const auto filterImage = static_cast<const FilterImage*>(image.get());
    auto filter = filterImage->filter;
    auto bound = Rect::MakeWH(filterImage->source->width(), filterImage->source->height());
    auto filtBound = filterImage->bounds;
    auto outer = Point::Make((filtBound.width() - bound.width()) / 2,
                             (filtBound.height() - bound.height()) / 2);
    auto offset =
        Point::Make((filtBound.centerX() - bound.centerX()), filtBound.centerY() - bound.centerY());
    bound = state.matrix.mapRect(bound);

    Resources resources;
    if (filter) {
      ElementWriter defs("defs", writer, resourceBucket.get());
      resources = defs.addImageFilterResource(filter, bound);
    }
    {
      auto groupElement = std::make_unique<ElementWriter>("g", writer, resourceBucket.get());
      if (!outer.isZero()) {
        groupElement->addAttribute(
            "transform", ToSVGTransform(Matrix::MakeTrans(outer.x - offset.x, outer.y - offset.y)));
      }
      if (filter) {
        groupElement->addAttribute("filter", resources.filter);
      }
      drawImage(filterImage->source, sampling, state, fill);
    }
  } else if (type == Types::ImageType::Subset) {
    const auto subsetImage = static_cast<const SubsetImage*>(image.get());
    auto bound = subsetImage->bounds.size();
    auto offset = Point::Make(subsetImage->bounds.x(), subsetImage->bounds.y());

    Path clipBound;
    clipBound.addRect(Rect::MakeSize(bound));
    applyClipPath(clipBound);
    auto groupElement = std::make_unique<ElementWriter>("g", writer, resourceBucket.get());
    if (!offset.isZero()) {
      groupElement->addAttribute("transform",
                                 ToSVGTransform(Matrix::MakeTrans(offset.x, offset.y)));
    }
    drawImage(subsetImage->source, sampling, state, fill);
  } else {
    Bitmap bitmap = ImageExportToBitmap(context, image);
    if (!bitmap.isEmpty()) {
      exportPixmap(Pixmap(bitmap), state, fill);
    }
  }
}

void SVGExportContext::drawImageRect(std::shared_ptr<Image> image, const Rect& srcRect,
                                     const Rect& dstRect, const SamplingOptions&,
                                     const MCState& state, const Fill& fill, SrcRectConstraint) {
  DEBUG_ASSERT(image != nullptr);
  auto subsetImage = image->makeSubset(srcRect);
  if (subsetImage == nullptr) {
    return;
  }
  Bitmap bitmap = ImageExportToBitmap(context, subsetImage);
  if (!bitmap.isEmpty()) {
    auto viewMatrix =
        MakeRectToRectMatrix(Rect::MakeWH(srcRect.width(), srcRect.height()), dstRect);

    MCState newState;
    newState.matrix = state.matrix;
    newState.matrix.preConcat(viewMatrix);

    auto fillMatrix = Matrix::I();
    viewMatrix.invert(&fillMatrix);

    exportPixmap(Pixmap(bitmap), newState, fill.makeWithMatrix(fillMatrix));
  }
}

void SVGExportContext::exportPixmap(const Pixmap& pixmap, const MCState& state, const Fill& fill) {
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
    ElementWriter imageUse("use", context, this, writer.get(), resourceBucket.get(),
                           exportFlags & SVGExportFlags::DisableWarnings, state, fill);
    imageUse.addAttribute("xlink:href", "#" + imageID);
  }
}

void SVGExportContext::drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList,
                                        const MCState& state, const Fill& fill,
                                        const Stroke* stroke) {
  DEBUG_ASSERT(glyphRunList != nullptr);
  auto typeface = glyphRunList->glyphRuns()[0].font.getTypeface().get();
  if (typeface == nullptr) {
    return;
  }
  // If the font needs to be converted to a path but lacks outlines (e.g., emoji font, web font),
  // it cannot be converted.
  auto deviceBounds = state.matrix.mapRect(glyphRunList->getBounds());
  if (!state.clip.contains(deviceBounds)) {
    applyClipPath(state.clip);
  }
  if (!typeface->isCustom()) {
    if (glyphRunList->hasOutlines() && !glyphRunList->hasColor() &&
        exportFlags & SVGExportFlags::ConvertTextToPaths) {
      exportGlyphsAsPath(glyphRunList, state, fill, stroke);
    } else {
      exportGlyphsAsText(glyphRunList, state, fill, stroke);
    }
  } else {
    if (glyphRunList->hasColor()) {
      exportGlyphsAsImage(glyphRunList, state, fill);
    } else {
      exportGlyphsAsPath(glyphRunList, state, fill, stroke);
    }
  }
}

void SVGExportContext::exportGlyphsAsPath(const std::shared_ptr<GlyphRunList>& glyphRunList,
                                          const MCState& state, const Fill& fill,
                                          const Stroke* stroke) {
  Path path;
  if (glyphRunList->getPath(&path)) {
    ElementWriter pathElement("path", context, this, writer.get(), resourceBucket.get(),
                              exportFlags & SVGExportFlags::DisableWarnings, state, fill, stroke);
    pathElement.addPathAttributes(path, tgfx::SVGExportContext::PathEncodingType());
    if (path.getFillType() == PathFillType::EvenOdd) {
      pathElement.addAttribute("fill-rule", "evenodd");
    }
  }
}

void SVGExportContext::exportGlyphsAsText(const std::shared_ptr<GlyphRunList>& glyphRunList,
                                          const MCState& state, const Fill& fill,
                                          const Stroke* stroke) {
  for (const auto& glyphRun : glyphRunList->glyphRuns()) {
    ElementWriter textElement("text", context, this, writer.get(), resourceBucket.get(),
                              exportFlags & SVGExportFlags::DisableWarnings, state, fill, stroke);

    textElement.addFontAttributes(glyphRun.font);

    auto unicharInfo = textBuilder.glyphToUnicharsInfo(glyphRun);
    textElement.addAttribute("x", unicharInfo.posX);
    textElement.addAttribute("y", unicharInfo.posY);
    textElement.addText(unicharInfo.text);
  }
}

void SVGExportContext::exportGlyphsAsImage(const std::shared_ptr<GlyphRunList>& glyphRunList,
                                           const MCState& state, const Fill& fill) {
  auto viewMatrix = state.matrix;
  auto scale = viewMatrix.getMaxScale();
  if (FloatNearlyZero(scale)) {
    return;
  }
  viewMatrix.preScale(1.0f / scale, 1.0f / scale);
  for (const auto& glyphRun : glyphRunList->glyphRuns()) {
    auto font = glyphRun.font;
    font = font.makeWithSize(scale * font.getSize());
    const auto& glyphIDs = glyphRun.glyphs;
    auto glyphCount = glyphIDs.size();
    const auto& positions = glyphRun.positions;
    auto glyphState = state;
    for (size_t i = 0; i < glyphCount; ++i) {
      const auto& glyphID = glyphIDs[i];
      const auto& position = positions[i];
      auto glyphCodec = font.getImage(glyphID, nullptr, &glyphState.matrix);
      auto glyphImage = Image::MakeFrom(glyphCodec);
      if (glyphImage == nullptr) {
        continue;
      }
      glyphState.matrix.postTranslate(position.x * scale, position.y * scale);
      glyphState.matrix.postConcat(viewMatrix);
      auto rect = Rect::MakeWH(glyphImage->width(), glyphImage->height());
      drawImageRect(std::move(glyphImage), rect, rect, {}, glyphState, fill,
                    SrcRectConstraint::Fast);
    }
  }
}

void SVGExportContext::drawPicture(std::shared_ptr<Picture> picture, const MCState& state) {
  DEBUG_ASSERT(picture != nullptr);
  picture->playback(this, state);
}

void SVGExportContext::drawLayer(std::shared_ptr<Picture> picture,
                                 std::shared_ptr<ImageFilter> imageFilter, const MCState& state,
                                 const Fill&) {
  DEBUG_ASSERT(picture != nullptr);
  Resources resources;
  if (imageFilter) {
    ElementWriter defs("defs", writer, resourceBucket.get());
    auto bound = picture->getBounds();
    resources = defs.addImageFilterResource(imageFilter, bound);
  }
  {
    if (!state.clip.contains(picture->getBounds())) {
      applyClipPath(state.clip);
    }
    auto groupElement = std::make_unique<ElementWriter>("g", writer, resourceBucket.get());
    if (imageFilter) {
      groupElement->addAttribute("filter", resources.filter);
    }
    picture->playback(this, state);
  }
}

bool SVGExportContext::RequiresViewportReset(const Fill& fill) {
  auto shader = fill.shader;
  if (!shader) {
    return false;
  }

  if (Types::Get(shader.get()) == Types::ShaderType::Image) {
    auto imageShader = static_cast<const ImageShader*>(shader.get());
    return imageShader->tileModeX == TileMode::Repeat || imageShader->tileModeY == TileMode::Repeat;
  }

  return false;
}

SVGPathParser::PathEncoding SVGExportContext::PathEncodingType() {
  return SVGPathParser::PathEncoding::Absolute;
}

void SVGExportContext::applyClipPath(const Path& clipPath) {
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
        element->addPathAttributes(clipPath, tgfx::SVGExportContext::PathEncodingType());
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

Bitmap SVGExportContext::ImageExportToBitmap(Context* context,
                                             const std::shared_ptr<Image>& image) {
  auto surface = Surface::Make(context, image->width(), image->height());
  auto canvas = surface->getCanvas();
  canvas->drawImage(image);

  Bitmap bitmap(surface->width(), surface->height(), false, true, surface->colorSpace());
  auto pixels = bitmap.lockPixels();
  if (surface->readPixels(bitmap.info(), pixels)) {
    bitmap.unlockPixels();
    return bitmap;
  }
  bitmap.unlockPixels();
  return Bitmap();
}

std::shared_ptr<Data> SVGExportContext::ImageToEncodedData(const std::shared_ptr<Image>& image) {
  if (Types::Get(image.get()) != Types::ImageType::Codec) {
    return nullptr;
  }
  const auto codecImage = static_cast<const CodecImage*>(image.get());
  auto imageCodec = codecImage->getCodec();
  return imageCodec->getEncodedData();
}

}  // namespace tgfx
