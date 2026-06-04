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
#include <utility>
#include "ElementWriter.h"
#include "SVGUtils.h"
#include "core/GlyphTransform.h"
#include "core/RunRecord.h"
#include "core/filters/GaussianBlurImageFilter.h"
#include "core/filters/ShaderMaskFilter.h"
#include "core/images/CodecImage.h"
#include "core/images/FilterImage.h"
#include "core/images/PictureImage.h"
#include "core/images/SubsetImage.h"
#include "core/shaders/ImageShader.h"
#include "core/shaders/MatrixShader.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "core/utils/RectToRectMatrix.h"
#include "core/utils/ShapeUtils.h"
#include "core/utils/Types.h"
#include "svg/SVGTextBuilder.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Brush.h"
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

// Detects the (BlendMode::Src + FilterImage<Blur> + ShaderMaskFilter) draw pattern produced by
// BackgroundBlurStyle's lower path.
static bool IsBackgroundBlurStylePattern(const Brush& brush, const Image* image) {
  if (brush.blendMode != BlendMode::Src) {
    return false;
  }
  if (!brush.maskFilter) {
    return false;
  }
  if (Types::Get(brush.maskFilter.get()) != Types::MaskFilterType::Shader) {
    return false;
  }
  if (Types::Get(image) != Types::ImageType::Filter) {
    return false;
  }
  const auto* filterImage = static_cast<const FilterImage*>(image);
  return filterImage->filter &&
         Types::Get(filterImage->filter.get()) == Types::ImageFilterType::Blur;
}

// Extracts the content PictureImage and accumulated shader matrix from a ShaderMaskFilter's
// shader chain. Returns nullptr if the chain does not lead to an ImageShader wrapping a
// PictureImage. Accumulates all MatrixShader transforms into shaderMatrix.
static const PictureImage* GetContentPictureImage(const MaskFilter* maskFilter,
                                                  Matrix* shaderMatrix) {
  if (Types::Get(maskFilter) != Types::MaskFilterType::Shader) {
    return nullptr;
  }
  auto shader = static_cast<const ShaderMaskFilter*>(maskFilter)->getShader();
  // Unwrap MatrixShader wrappers, accumulating their transforms.
  *shaderMatrix = Matrix::I();
  while (shader && Types::Get(shader.get()) == Types::ShaderType::Matrix) {
    auto* matrixShader = static_cast<const MatrixShader*>(shader.get());
    shaderMatrix->preConcat(matrixShader->matrix);
    shader = matrixShader->source;
  }
  if (!shader || Types::Get(shader.get()) != Types::ShaderType::Image) {
    return nullptr;
  }
  auto image = static_cast<const ImageShader*>(shader.get())->image;
  if (image && Types::Get(image.get()) == Types::ImageType::Picture) {
    return static_cast<const PictureImage*>(image.get());
  }
  return nullptr;
}

SVGExportContext::SVGExportContext(Context* context, const Rect& viewBox,
                                   std::unique_ptr<XMLWriter> inputXmlWriter, uint32_t exportFlags,
                                   std::shared_ptr<SVGCustomWriter> customWriter,
                                   std::shared_ptr<ColorSpace> targetColorSpace,
                                   std::shared_ptr<ColorSpace> assignColorSpace)
    : exportFlags(exportFlags), context(context), viewBox(viewBox),
      xmlWriter(std::move(inputXmlWriter)), resourceBucket(new ResourceStore),
      customWriter(std::move(customWriter)), _targetColorSpace(std::move(targetColorSpace)),
      _assignColorSpace(std::move(assignColorSpace)) {
  if (viewBox.isEmpty()) {
    return;
  }

  xmlWriter->writeHeader();
  // The root <svg> tag gets closed by the destructor.
  rootElement = std::make_unique<ElementWriter>("svg", xmlWriter);

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

void SVGExportContext::drawFill(const Brush& brush) {
  drawRect(viewBox, {}, ClipStack(), brush, nullptr);
}

void SVGExportContext::drawRect(const Rect& rect, const Matrix& matrix, const ClipStack& clip,
                                const Brush& brush, const Stroke*) {
  std::unique_ptr<ElementWriter> svg;
  if (RequiresViewportReset(brush)) {
    svg =
        std::make_unique<ElementWriter>("svg", context, this, xmlWriter.get(), resourceBucket.get(),
                                        exportFlags & SVGExportFlags::DisableWarnings, matrix,
                                        brush, nullptr, _targetColorSpace, _assignColorSpace);
    svg->addRectAttributes(rect);
  }

  applyClip(clip, matrix.mapRect(rect));

  ElementWriter rectElement("rect", context, this, xmlWriter.get(), resourceBucket.get(),
                            exportFlags & SVGExportFlags::DisableWarnings, matrix, brush, nullptr,
                            _targetColorSpace, _assignColorSpace);

  if (svg) {
    rectElement.addAttribute("x", 0);
    rectElement.addAttribute("y", 0);
    rectElement.addAttribute("width", "100%");
    rectElement.addAttribute("height", "100%");
  } else {
    rectElement.addRectAttributes(rect);
  }
}

void SVGExportContext::drawRRect(const RRect& roundRect, const Matrix& matrix,
                                 const ClipStack& clip, const Brush& brush, const Stroke*) {
  applyClip(clip, matrix.mapRect(roundRect.rect()));
  if (roundRect.isOval()) {
    if (roundRect.rect().width() == roundRect.rect().height()) {
      ElementWriter circleElement("circle", context, this, xmlWriter.get(), resourceBucket.get(),
                                  exportFlags & SVGExportFlags::DisableWarnings, matrix, brush,
                                  nullptr, _targetColorSpace, _assignColorSpace);
      circleElement.addCircleAttributes(roundRect.rect());
      return;
    } else {
      ElementWriter ovalElement("ellipse", context, this, xmlWriter.get(), resourceBucket.get(),
                                exportFlags & SVGExportFlags::DisableWarnings, matrix, brush,
                                nullptr, _targetColorSpace, _assignColorSpace);
      ovalElement.addEllipseAttributes(roundRect.rect());
    }
  } else if (roundRect.isComplex()) {
    // SVG <rect> only supports uniform rx/ry. Complex RRects must be exported as <path>.
    Path path = {};
    path.addRRect(roundRect);
    drawPath(path, matrix, clip, brush);
  } else {
    ElementWriter rrectElement("rect", context, this, xmlWriter.get(), resourceBucket.get(),
                               exportFlags & SVGExportFlags::DisableWarnings, matrix, brush,
                               nullptr, _targetColorSpace, _assignColorSpace);
    rrectElement.addRoundRectAttributes(roundRect);
  }
}

void SVGExportContext::drawPath(const Path& path, const Matrix& matrix, const ClipStack& clip,
                                const Brush& brush) {
  applyClip(clip, matrix.mapRect(path.getBounds()));
  ElementWriter pathElement("path", context, this, xmlWriter.get(), resourceBucket.get(),
                            exportFlags & SVGExportFlags::DisableWarnings, matrix, brush, nullptr,
                            _targetColorSpace, _assignColorSpace);
  pathElement.addPathAttributes(path, tgfx::SVGExportContext::PathEncodingType());
  if (path.getFillType() == PathFillType::EvenOdd) {
    pathElement.addAttribute("fill-rule", "evenodd");
  }
}

void SVGExportContext::drawShape(std::shared_ptr<Shape> shape, const Matrix& matrix,
                                 const ClipStack& clip, const Brush& brush, const Stroke* stroke) {
  DEBUG_ASSERT(shape != nullptr);
  shape = Shape::ApplyStroke(shape, stroke);
  auto path = ShapeUtils::GetShapeRenderingPath(shape, matrix.getMaxScale());
  drawPath(path, matrix, clip, brush);
}

void SVGExportContext::drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                                 const Matrix& matrix, const ClipStack& clip, const Brush& brush) {
  DEBUG_ASSERT(image != nullptr);
  auto type = Types::Get(image.get());
  if (type == Types::ImageType::Picture) {
    if (brush.nothingToDraw()) {
      return;
    }
    // Keep PictureImage vectorized when the brush only carries effects SVG <g> can express:
    // alpha as opacity, Blend/Matrix colorFilter as a <filter> resource, and a separable blend
    // mode as mix-blend-mode. Anything else (shader, maskFilter, Porter-Duff blend without an
    // SVG mapping, or unsupported colorFilter subtype) rasterizes.
    bool canVectorize = brush.shader == nullptr && brush.maskFilter == nullptr;
    std::string blendModeStyle;
    if (canVectorize && brush.blendMode != BlendMode::SrcOver) {
      auto svgBlendMode = ToSVGBlendMode(brush.blendMode);
      if (svgBlendMode.empty() || svgBlendMode == "normal") {
        canVectorize = false;
      } else {
        blendModeStyle = "mix-blend-mode: " + svgBlendMode;
      }
    }
    std::shared_ptr<ColorFilter> vectorColorFilter;
    if (canVectorize && brush.colorFilter) {
      auto colorFilterType = Types::Get(brush.colorFilter.get());
      // Decide expressibility WITHOUT writing <defs> yet; the actual <defs>/<filter> emission is
      // deferred to exportPictureImageAsVector so it lands AFTER the outer clip wrapper has been
      // closed and at the same XML level as the brush <g> that consumes it.
      bool expressible =
          colorFilterType == Types::ColorFilterType::Matrix ||
          (colorFilterType == Types::ColorFilterType::Blend &&
           !ToSVGBlendMode(static_cast<const ModeColorFilter*>(brush.colorFilter.get())->mode)
                .empty());
      if (!expressible) {
        canVectorize = false;
      } else {
        vectorColorFilter = brush.colorFilter;
      }
    }
    if (canVectorize) {
      const auto pictureImage = static_cast<const PictureImage*>(image.get());
      auto newMatrix = matrix;
      if (pictureImage->matrix) {
        newMatrix.preConcat(*pictureImage->matrix);
      }
      exportPictureImageAsVector(pictureImage, newMatrix, clip, vectorColorFilter, blendModeStyle,
                                 brush.color.alpha);
      return;
    }
    auto modifyImage = ConvertImageColorSpace(image, context, _targetColorSpace, _assignColorSpace);
    Bitmap bitmap = ImageExportToBitmap(context, modifyImage);
    if (!bitmap.isEmpty()) {
      applyClip(clip, matrix.mapRect(Rect::MakeWH(image->width(), image->height())));
      exportPixmap(Pixmap(bitmap), matrix, brush);
    }
  } else if (type == Types::ImageType::Filter) {
    const auto filterImage = static_cast<const FilterImage*>(image.get());
    auto filter = filterImage->filter;

    // BackgroundBlurStyle lowers to (BlendMode::Src + FilterImage<Blur> + ShaderMaskFilter).
    // Output as <foreignObject> with backdrop-filter:blur() for native SVG backdrop blur.
    if (IsBackgroundBlurStylePattern(brush, image.get())) {
      Matrix shaderMatrix = Matrix::I();
      auto* contentPictureImage = GetContentPictureImage(brush.maskFilter.get(), &shaderMatrix);
      if (contentPictureImage) {
        const auto* blurFilter = static_cast<const GaussianBlurImageFilter*>(filter.get());
        auto contentBounds =
            Rect::MakeWH(contentPictureImage->width(), contentPictureImage->height());
        // Apply the PictureImage's matrix and the accumulated shader matrix to get the content
        // bounds in the draw coordinate space. The shader matrix captures the inverse of the
        // drawImage offset, which compensates for the backdrop blur image's offset relative to
        // the layer's content position.
        auto contentMatrix = matrix;
        contentMatrix.preConcat(shaderMatrix);
        if (contentPictureImage->matrix) {
          contentMatrix.preConcat(*contentPictureImage->matrix);
        }
        auto mappedContentBounds = contentMatrix.mapRect(contentBounds);

        // foreignObject must extend beyond the content bounds by the blur outset so the blur
        // has room to render outside the clipped area. Compute the blur outset in SVG user space.
        auto canvasScale = contentMatrix.getMaxScale();
        // blurFilter->blurrinessX/Y are in pixel space (sigma * contentScale).
        // canvasScale ≈ 1/contentScale, so user-space sigma ≈ blurriness * canvasScale.
        auto userSigmaX = FloatNearlyZero(canvasScale) ? blurFilter->blurrinessX
                                                       : blurFilter->blurrinessX * canvasScale;
        auto userSigmaY = FloatNearlyZero(canvasScale) ? blurFilter->blurrinessY
                                                       : blurFilter->blurrinessY * canvasScale;
        // onFilterBounds outsets by 2*sigma per side.
        auto foBounds = mappedContentBounds;
        foBounds.outset(2.0f * userSigmaX, 2.0f * userSigmaY);

        // Close any active clip group before emitting resources.
        clipGroupElement = nullptr;
        currentClipPath = {};

        // Define clipPath from content picture's vector shape.
        auto clipID = resourceBucket->addClip();
        {
          ElementWriter defsElement("defs", xmlWriter);
          ElementWriter clipPathElement("clipPath", xmlWriter);
          clipPathElement.addAttribute("id", clipID);
          // The clipPath coordinates are in the foreignObject's local space, so translate by
          // the foreignObject origin offset.
          auto foOffset = Point::Make(foBounds.x(), foBounds.y());
          clipPathElement.addAttribute("transform",
                                       ToSVGTransform(Matrix::MakeTrans(-foOffset.x, -foOffset.y)));
          // Playback the content picture to generate vector clip shapes. SVG <clipPath> only
          // recognizes basic shape elements (path/rect/circle/etc.) as clip geometry; brush
          // attributes (fill/stroke/filter) are ignored, and non-shape descendants (e.g.
          // <image>, nested <filter>) are not portable across renderers. Works reliably for
          // BackgroundBlurStyle's typical use (single ShapeLayer content). Layers with
          // image/text/filter content under backdrop blur may render inconsistently — fall
          // through to raster fallback if richer content support is needed.
          drawPicture(contentPictureImage->picture, contentMatrix, ClipStack());
        }

        // Output <foreignObject> with backdrop-filter:blur().
        // CSS blur() parameter is the Gaussian sigma in the foreignObject's coordinate space.
        xmlWriter->startElement("foreignObject");
        xmlWriter->addScalarAttribute("x", foBounds.x());
        xmlWriter->addScalarAttribute("y", foBounds.y());
        xmlWriter->addScalarAttribute("width", foBounds.width());
        xmlWriter->addScalarAttribute("height", foBounds.height());
        {
          xmlWriter->startElement("div");
          xmlWriter->addAttribute("xmlns", "http://www.w3.org/1999/xhtml");
          // CSS backdrop-filter:blur() accepts only a single (isotropic) radius. When the
          // source filter is anisotropic (sigmaX != sigmaY), take the larger axis as a
          // best-effort approximation — exact anisotropy cannot be expressed in CSS.
          auto blurSigma = std::max(userSigmaX, userSigmaY);
          std::string style = "backdrop-filter:blur(" + FloatToString(blurSigma) + "px);" +
                              "clip-path:url(#" + clipID + ");height:100%;width:100%";
          xmlWriter->addAttribute("style", style);
          xmlWriter->addText("");
          xmlWriter->endElement();  // div
        }
        xmlWriter->endElement();  // foreignObject

        clipGroupElement = nullptr;
        currentClipPath = {};
        return;
      }
      // Fallback: content image is not a PictureImage, cannot extract vector clip path.
      // Fall through to the default FilterImage rasterization path.
    }

    auto bound = Rect::MakeWH(filterImage->source->width(), filterImage->source->height());
    auto filtBound = filterImage->bounds;
    auto outer = Point::Make((filtBound.width() - bound.width()) / 2,
                             (filtBound.height() - bound.height()) / 2);
    auto offset =
        Point::Make((filtBound.centerX() - bound.centerX()), filtBound.centerY() - bound.centerY());
    // Source is drawn with the matrix preTranslated by (outer - offset) below (see innerMatrix).
    // The filter region must follow the same matrix to stay aligned in user space; using the raw
    // `matrix` here would place the <filter> region at the pre-compensation location while the
    // content renders at the compensated location, leaving the filter region clipped on one side
    // (cutting off the blur halo) and extending into empty sandbox space on the other (creating
    // hard-edged banding from the implicit source edge at the sandbox boundary).
    auto contentMatrix = matrix;
    contentMatrix.preTranslate(outer.x - offset.x, outer.y - offset.y);
    bound = contentMatrix.mapRect(bound);

    Resources resources;
    if (filter) {
      ElementWriter defs("defs", xmlWriter, resourceBucket.get(), _targetColorSpace,
                         _assignColorSpace);
      resources = defs.addImageFilterResource(filter, bound, customWriter);
    }
    auto clipPath = clip.getClipPath();
    bool needsClip = !clipPath.isEmpty() && !clipPath.contains(bound);
    // Close any active clip group from prior draws BEFORE emitting our own <clipPath> resource
    // and brush <g>. Otherwise the <clipPath> definition would be nested inside the stale wrapper
    // and the new <g> would sit at the wrong XML depth. On exit, leave both members cleared so
    // the next applyClipPath rebuilds the clip group instead of short-circuiting on a matching
    // currentClipPath.
    clipGroupElement = nullptr;
    currentClipPath = {};
    std::string clipID;
    if (needsClip) {
      clipID = defineClipPath(clipPath);
    }
    // Collapse clip-path, filter, mix-blend-mode and opacity onto a single <g>. Wrapping the
    // mix-blend-mode element in any extra parent <g> (e.g. one that only carries clip-path)
    // would make its backdrop the empty content of that wrapper instead of the real previous
    // siblings, causing the blend to silently fail. SVG/CSS painting model applies clip before
    // filter on the same element, so co-locating them is semantically equivalent to nesting.
    std::string outerBlendStyle;
    if (brush.blendMode != BlendMode::SrcOver) {
      auto svgBlendMode = ToSVGBlendMode(brush.blendMode);
      if (!svgBlendMode.empty() && svgBlendMode != "normal") {
        outerBlendStyle = "mix-blend-mode: " + svgBlendMode;
      }
    }
    float outerAlpha = brush.color.alpha;
    {
      auto groupElement = std::make_unique<ElementWriter>("g", xmlWriter, resourceBucket.get());
      if (needsClip) {
        groupElement->addAttribute("clip-path", "url(#" + clipID + ")");
      }
      if (filter) {
        groupElement->addAttribute("filter", resources.filter);
      }
      if (!outerBlendStyle.empty()) {
        groupElement->addAttribute("style", outerBlendStyle);
      }
      if (outerAlpha != 1.0f) {
        groupElement->addAttribute("opacity", outerAlpha);
      }
      // Strip lifted effects from the inner brush so the recursive draw does not re-emit them
      // on a nested <g>, which would both double the effect and reintroduce the isolation issue.
      Brush innerBrush = brush;
      if (!outerBlendStyle.empty()) {
        innerBrush.blendMode = BlendMode::SrcOver;
      }
      if (outerAlpha != 1.0f) {
        innerBrush.color.alpha = 1.0f;
      }
      // contentMatrix already pre-translates by (outer - offset) so the source is drawn at its
      // intended user-space position. Pre-concatenating this offset into the matrix (instead of
      // writing it as the group's transform) keeps the source-local offset correctly oriented
      // under any rotation/scale in the layer matrix, matching the raster pipeline in
      // Canvas::drawLayer (which uses drawMatrix.preTranslate(filterOffset)).
      drawImage(filterImage->source, sampling, contentMatrix, needsClip ? ClipStack{} : clip,
                innerBrush);
      clipGroupElement = nullptr;
      currentClipPath = {};
    }
  } else if (type == Types::ImageType::Subset) {
    const auto subsetImage = static_cast<const SubsetImage*>(image.get());
    auto bound = subsetImage->bounds.size();
    auto offset = Point::Make(subsetImage->bounds.x(), subsetImage->bounds.y());

    Path clipBound;
    clipBound.addRect(Rect::MakeSize(bound));
    applyClipPath(clipBound);
    auto groupElement = std::make_unique<ElementWriter>("g", xmlWriter, resourceBucket.get());
    if (!offset.isZero()) {
      groupElement->addAttribute("transform",
                                 ToSVGTransform(Matrix::MakeTrans(offset.x, offset.y)));
    }
    drawImage(subsetImage->source, sampling, matrix, clip, brush);
  } else {
    auto modifyImage = ConvertImageColorSpace(image, context, _targetColorSpace, _assignColorSpace);
    Bitmap bitmap = ImageExportToBitmap(context, modifyImage);
    if (!bitmap.isEmpty()) {
      exportPixmap(Pixmap(bitmap), matrix, brush);
    }
  }
}

void SVGExportContext::drawImageRect(std::shared_ptr<Image> image, const Rect& srcRect,
                                     const Rect& dstRect, const SamplingOptions&,
                                     const Matrix& matrix, const ClipStack& clip,
                                     const Brush& brush, SrcRectConstraint) {
  DEBUG_ASSERT(image != nullptr);
  auto modifyImage = ConvertImageColorSpace(image, context, _targetColorSpace, _assignColorSpace);
  auto subsetImage = modifyImage->makeSubset(srcRect);
  if (subsetImage == nullptr) {
    return;
  }
  Bitmap bitmap = ImageExportToBitmap(context, subsetImage);
  if (!bitmap.isEmpty()) {
    applyClip(clip, matrix.mapRect(dstRect));

    auto viewMatrix =
        MakeRectToRectMatrix(Rect::MakeWH(srcRect.width(), srcRect.height()), dstRect);

    auto newMatrix = matrix;
    newMatrix.preConcat(viewMatrix);

    auto fillMatrix = Matrix::I();
    viewMatrix.invert(&fillMatrix);

    exportPixmap(Pixmap(bitmap), newMatrix, brush.makeWithMatrix(fillMatrix));
  }
}

void SVGExportContext::exportPixmap(const Pixmap& pixmap, const Matrix& matrix,
                                    const Brush& brush) {
  auto dataUri = AsDataUri(pixmap);
  if (!dataUri) {
    return;
  }

  std::string imageID = resourceBucket->addImage();
  {
    ElementWriter defElement("defs", xmlWriter);
    {
      ElementWriter imageElement("image", xmlWriter);
      imageElement.addAttribute("id", imageID);
      imageElement.addAttribute("width", pixmap.width());
      imageElement.addAttribute("height", pixmap.height());
      imageElement.addAttribute("xlink:href", static_cast<const char*>(dataUri->data()));
    }
  }
  {
    ElementWriter imageUse("use", context, this, xmlWriter.get(), resourceBucket.get(),
                           exportFlags & SVGExportFlags::DisableWarnings, matrix, brush, nullptr,
                           _targetColorSpace, _assignColorSpace);
    imageUse.addAttribute("xlink:href", "#" + imageID);
  }
}

void SVGExportContext::drawTextBlob(std::shared_ptr<TextBlob> textBlob, const Matrix& matrix,
                                    const ClipStack& clip, const Brush& brush,
                                    const Stroke* stroke) {
  DEBUG_ASSERT(textBlob != nullptr);
  auto deviceBounds = matrix.mapRect(textBlob->getBounds());
  applyClip(clip, deviceBounds);
  for (auto glyphRun : *textBlob) {
    auto typeface = glyphRun.font.getTypeface();
    if (typeface == nullptr) {
      continue;
    }
    // RSXform/Matrix positioning requires path export since SVG <text> cannot represent per-glyph
    // rotation/scale.
    if (!typeface->isCustom()) {
      if (HasComplexTransform(glyphRun) ||
          (glyphRun.font.hasOutlines() && !glyphRun.font.hasColor() &&
           exportFlags & SVGExportFlags::ConvertTextToPaths)) {
        exportGlyphRunAsPath(glyphRun, matrix, brush, stroke);
      } else {
        exportGlyphRunAsText(glyphRun, matrix, brush, stroke);
      }
    } else {
      if (glyphRun.font.hasColor()) {
        exportGlyphRunAsImage(glyphRun, matrix, clip, brush);
      } else {
        exportGlyphRunAsPath(glyphRun, matrix, brush, stroke);
      }
    }
  }
}

void SVGExportContext::exportGlyphRunAsPath(const GlyphRun& glyphRun, const Matrix& matrix,
                                            const Brush& brush, const Stroke* stroke) {
  Path path = {};
  auto& font = glyphRun.font;
  size_t glyphCount = glyphRun.glyphCount;
  for (size_t index = 0; index < glyphCount; ++index) {
    auto glyphID = glyphRun.glyphs[index];
    Path glyphPath = {};
    if (font.getPath(glyphID, &glyphPath)) {
      auto glyphMatrix = GetGlyphMatrix(glyphRun, index);
      glyphPath.transform(glyphMatrix);
      path.addPath(glyphPath);
    }
  }
  if (path.isEmpty()) {
    return;
  }
  ElementWriter pathElement("path", context, this, xmlWriter.get(), resourceBucket.get(),
                            exportFlags & SVGExportFlags::DisableWarnings, matrix, brush, stroke,
                            _targetColorSpace, _assignColorSpace);
  pathElement.addPathAttributes(path, tgfx::SVGExportContext::PathEncodingType());
  if (path.getFillType() == PathFillType::EvenOdd) {
    pathElement.addAttribute("fill-rule", "evenodd");
  }
}

void SVGExportContext::exportGlyphRunAsText(const GlyphRun& glyphRun, const Matrix& matrix,
                                            const Brush& brush, const Stroke* stroke) {
  ElementWriter textElement("text", context, this, xmlWriter.get(), resourceBucket.get(),
                            exportFlags & SVGExportFlags::DisableWarnings, matrix, brush, stroke,
                            _targetColorSpace, _assignColorSpace);

  textElement.addFontAttributes(glyphRun.font);

  auto unicharInfo = textBuilder.glyphToUnicharsInfo(glyphRun);
  textElement.addAttribute("x", unicharInfo.posX);
  textElement.addAttribute("y", unicharInfo.posY);
  textElement.addText(unicharInfo.text);
}

void SVGExportContext::exportGlyphRunAsImage(const GlyphRun& glyphRun, const Matrix& matrix,
                                             const ClipStack& clip, const Brush& brush) {
  auto scale = matrix.getMaxScale();
  if (FloatNearlyZero(scale)) {
    return;
  }
  auto font = glyphRun.font.makeWithSize(scale * glyphRun.font.getSize());
  size_t glyphCount = glyphRun.glyphCount;
  for (size_t i = 0; i < glyphCount; ++i) {
    auto glyphID = glyphRun.glyphs[i];
    auto glyphRunMatrix = GetGlyphMatrix(glyphRun, i);
    auto glyphMatrix = matrix;
    auto glyphCodec = font.getImage(glyphID, nullptr, &glyphMatrix);
    auto glyphImage = Image::MakeFrom(glyphCodec);
    if (glyphImage == nullptr) {
      continue;
    }
    glyphMatrix.postScale(1.0f / scale, 1.0f / scale);
    glyphMatrix.postConcat(glyphRunMatrix);
    glyphMatrix.postConcat(matrix);
    auto rect = Rect::MakeWH(glyphImage->width(), glyphImage->height());
    drawImageRect(std::move(glyphImage), rect, rect, {}, glyphMatrix, clip, brush,
                  SrcRectConstraint::Fast);
  }
}

void SVGExportContext::drawPicture(std::shared_ptr<Picture> picture, const Matrix& matrix,
                                   const ClipStack& clip) {
  DEBUG_ASSERT(picture != nullptr);
  picture->playback(this, matrix, clip);
}

void SVGExportContext::exportPictureImageAsVector(const PictureImage* pictureImage,
                                                  const Matrix& matrix, const ClipStack& clip,
                                                  const std::shared_ptr<ColorFilter>& colorFilter,
                                                  const std::string& blendModeStyle, float alpha) {
  // Skip the wrapper when there is nothing to attach to it.
  if (!colorFilter && blendModeStyle.empty() && alpha == 1.0f) {
    drawPicture(pictureImage->picture, matrix, clip);
    return;
  }
  // Lift the outer clip to a single <g clip-path> wrapping the brush group so nested playback
  // ops do not redefine identical clipPath resources. See drawLayer for the same pattern.
  // Use the PictureImage's logical output rect (not the raw picture bounds) so the contains()
  // check matches what is actually painted into the SVG.
  auto clipPath = clip.getClipPath();
  auto bound = matrix.mapRect(Rect::MakeWH(static_cast<float>(pictureImage->width()),
                                           static_cast<float>(pictureImage->height())));
  bool needsClip = !clipPath.isEmpty() && !clipPath.contains(bound);
  {
    // Close any clip wrapper opened by previous draws BEFORE emitting our <clipPath>, <defs>
    // and brush <g>. Otherwise these resources would be nested inside a stale <g clip-path>
    // that does not belong to this draw, leaving them at the wrong XML depth.
    clipGroupElement = nullptr;
    currentClipPath = {};
    std::string clipID;
    if (needsClip) {
      clipID = defineClipPath(clipPath);
    }
    std::string filterUrl;
    if (colorFilter) {
      ElementWriter defs("defs", xmlWriter, resourceBucket.get(), _targetColorSpace,
                         _assignColorSpace);
      filterUrl = defs.addColorFilterResource(colorFilter).filter;
    }
    std::unique_ptr<ElementWriter> clipElement;
    if (needsClip) {
      clipElement = std::make_unique<ElementWriter>("g", xmlWriter, resourceBucket.get());
      clipElement->addAttribute("clip-path", "url(#" + clipID + ")");
    }
    ElementWriter brushGroup("g", xmlWriter, resourceBucket.get());
    if (!filterUrl.empty()) {
      brushGroup.addAttribute("filter", filterUrl);
    }
    if (!blendModeStyle.empty()) {
      brushGroup.addAttribute("style", blendModeStyle);
    }
    if (alpha != 1.0f) {
      brushGroup.addAttribute("opacity", alpha);
    }
    drawPicture(pictureImage->picture, matrix, needsClip ? ClipStack{} : clip);
    clipGroupElement = nullptr;
    currentClipPath = {};
  }
}

void SVGExportContext::drawLayer(std::shared_ptr<Picture> picture,
                                 std::shared_ptr<ImageFilter> imageFilter, const Matrix& matrix,
                                 const ClipStack& clip, const Brush& brush) {
  DEBUG_ASSERT(picture != nullptr);
  Resources resources;
  auto bound = matrix.mapRect(picture->getBounds());
  if (imageFilter) {
    ElementWriter defs("defs", xmlWriter, resourceBucket.get(), _targetColorSpace,
                       _assignColorSpace);
    // The <filter> element uses filterUnits="userSpaceOnUse", so its x/y/width/height must be
    // in user space. Passing the picture-local bounds here mis-locates the filter region
    // whenever the matrix is non-identity, which both cuts off the blur halo on one side and
    // exposes the filter sandbox edges (the empty area inside the filter region but outside
    // the source) as hard banding artifacts.
    resources = defs.addImageFilterResource(imageFilter, bound, customWriter);
  }
  auto clipPath = clip.getClipPath();
  bool needsClip = !clipPath.isEmpty() && !clipPath.contains(bound);
  std::string clipID;
  if (needsClip) {
    clipID = defineClipPath(clipPath);
  }
  {
    // See drawImage FilterImage branch for why the active clip state is cleared here and
    // left cleared on exit instead of being saved and restored.
    clipGroupElement = nullptr;
    currentClipPath = {};
    auto groupElement = std::make_unique<ElementWriter>("g", xmlWriter, resourceBucket.get());
    if (needsClip) {
      groupElement->addAttribute("clip-path", "url(#" + clipID + ")");
    }
    if (imageFilter) {
      groupElement->addAttribute("filter", resources.filter);
    }
    if (brush.blendMode != BlendMode::SrcOver) {
      auto svgBlendMode = ToSVGBlendMode(brush.blendMode);
      if (!svgBlendMode.empty() && svgBlendMode != "normal") {
        groupElement->addAttribute("style", "mix-blend-mode: " + svgBlendMode);
      }
    }
    if (brush.color.alpha != 1.0f) {
      groupElement->addAttribute("opacity", brush.color.alpha);
    }
    picture->playback(this, matrix, needsClip ? ClipStack{} : clip);
    clipGroupElement = nullptr;
    currentClipPath = {};
  }
}

bool SVGExportContext::RequiresViewportReset(const Brush& brush) {
  auto shader = brush.shader;
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

void SVGExportContext::applyClip(const ClipStack& clip, const Rect& contentBound) {
  // SVG export merges all clip elements into a single path to reduce file complexity.
  // Per-element anti-alias info is lost; the merged path's AA is left to the SVG renderer.
  auto clipPath = clip.getClipPath();
  if (clipPath.contains(contentBound)) {
    // No clip needed for this draw, but a previous draw may have left a <g clip-path>
    // wrapper open. Only close it when the active wrapper's clip differs from ours;
    // matching wrappers are kept so consecutive same-clip draws keep sharing one group.
    if (clipPath != currentClipPath) {
      clipGroupElement = nullptr;
      currentClipPath = {};
    }
    return;
  }
  applyClipPath(clipPath);
}

std::string SVGExportContext::defineClipPath(const Path& clipPath) {
  std::string clipID = resourceBucket->addClip();
  ElementWriter clipPathElement("clipPath", xmlWriter);
  clipPathElement.addAttribute("id", clipID);
  {
    std::unique_ptr<ElementWriter> element;
    Rect rect;
    RRect rrect;
    Rect ovalBound;
    if (clipPath.isRect(&rect)) {
      element = std::make_unique<ElementWriter>("rect", xmlWriter);
      element->addRectAttributes(rect);
    } else if (clipPath.isRRect(&rrect) && !rrect.isComplex()) {
      element = std::make_unique<ElementWriter>("rect", xmlWriter);
      element->addRoundRectAttributes(rrect);
    } else if (clipPath.isOval(&ovalBound)) {
      if (FloatNearlyEqual(ovalBound.width(), ovalBound.height())) {
        element = std::make_unique<ElementWriter>("circle", xmlWriter);
        element->addCircleAttributes(ovalBound);
      } else {
        element = std::make_unique<ElementWriter>("ellipse", xmlWriter);
        element->addEllipseAttributes(ovalBound);
      }
    } else {
      element = std::make_unique<ElementWriter>("path", xmlWriter);
      element->addPathAttributes(clipPath, tgfx::SVGExportContext::PathEncodingType());
      if (clipPath.getFillType() == PathFillType::EvenOdd) {
        element->addAttribute("clip-rule", "evenodd");
      }
    }
  }
  return clipID;
}

void SVGExportContext::applyClipPath(const Path& clipPath) {
  if (clipPath == currentClipPath) {
    return;
  }
  clipGroupElement = nullptr;
  if (clipPath.isEmpty()) {
    return;
  }
  currentClipPath = clipPath;
  auto clipID = defineClipPath(currentClipPath);
  clipGroupElement = std::make_unique<ElementWriter>("g", xmlWriter);
  clipGroupElement->addAttribute("clip-path", "url(#" + clipID + ")");
}

Bitmap SVGExportContext::ImageExportToBitmap(Context* context,
                                             const std::shared_ptr<Image>& image) {
  auto surface = Surface::Make(context, image->width(), image->height());
  auto canvas = surface->getCanvas();
  canvas->drawImage(image);

  Bitmap bitmap(surface->width(), surface->height(), false, true, image->colorSpace());
  auto pixels = bitmap.lockPixels();
  if (surface->readPixels(bitmap.info().makeColorSpace(nullptr), pixels)) {
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
