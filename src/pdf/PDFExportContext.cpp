/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "PDFExportContext.h"
#include <cmath>
#include <cstddef>
#include <functional>
#include <memory>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>
#include "core/DrawContext.h"
#include "core/MCState.h"
#include "core/MeasureContext.h"
#include "core/Records.h"
#include "core/TransformContext.h"
#include "core/TypefaceMetrics.h"
#include "core/filters/DropShadowImageFilter.h"
#include "core/filters/ShaderMaskFilter.h"
#include "core/images/PictureImage.h"
#include "core/shaders/ImageShader.h"
#include "core/utils/Caster.h"
#include "core/utils/Log.h"
#include "pdf/PDFBitmap.h"
#include "pdf/PDFDocument.h"
#include "pdf/PDFFormXObject.h"
#include "pdf/PDFGraphicState.h"
#include "pdf/PDFResourceDictionary.h"
#include "pdf/PDFShader.h"
#include "pdf/PDFTypes.h"
#include "pdf/PDFUtils.h"
#include "tgfx/core/AlphaType.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/Buffer.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/ColorType.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Fill.h"
#include "tgfx/core/Font.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/ImageBuffer.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/ImageInfo.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/PathTypes.h"
#include "tgfx/core/Picture.h"
#include "tgfx/core/Pixmap.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/SamplingOptions.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/Size.h"
#include "tgfx/core/Stream.h"
#include "tgfx/core/Surface.h"
#include "tgfx/core/TileMode.h"
#include "tgfx/core/WriteStream.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {

// A helper class to automatically finish a ContentEntry at the end of a
// drawing method and maintain the state needed between set up and finish.
class ScopedContentEntry {
 public:
  ScopedContentEntry(PDFExportContext* device, const MCState* state, const Matrix& matrix,
                     const Fill& fill, float textScale = 0)
      : drawContext(device), state(state) {
    blendMode = fill.blendMode;
    contentStream =
        drawContext->setUpContentEntry(state, matrix, fill, textScale, &destFormXObject);
  }

  // ScopedContentEntry(PDFExportContext* dev, const Paint& paint, float textScale = 0)
  //     : ScopedContentEntry(dev, &dev->cs(), dev->localToDevice(), paint, textScale) {
  // }

  ~ScopedContentEntry() {
    if (contentStream) {
      Path* shape = &path;
      if (shape->isEmpty()) {
        shape = nullptr;
      }
      drawContext->finishContentEntry(state, blendMode, destFormXObject, shape);
    }
  }

  explicit operator bool() const {
    return contentStream != nullptr;
  }

  std::shared_ptr<MemoryWriteStream> stream() {
    return contentStream;
  }

  /* Returns true when we explicitly need the shape of the drawing. */
  bool needShape() {
    switch (blendMode) {
      case BlendMode::Clear:
      case BlendMode::Src:
      case BlendMode::SrcIn:
      case BlendMode::SrcOut:
      case BlendMode::DstIn:
      case BlendMode::DstOut:
      case BlendMode::SrcATop:
      case BlendMode::DstATop:
      case BlendMode::Modulate:
        return true;
      default:
        return false;
    }
  }

  /* Returns true unless we only need the shape of the drawing. */
  bool needSource() {
    return blendMode != BlendMode::Clear;
  }

  /* If the shape is different than the alpha component of the content, then
     * setShape should be called with the shape.  In particular, images and
     * devices have rectangular shape.
     */
  void setShape(const Path& shape) {
    path = shape;
  }

 private:
  PDFExportContext* drawContext = nullptr;
  std::shared_ptr<MemoryWriteStream> contentStream = nullptr;
  BlendMode blendMode = {};
  PDFIndirectReference destFormXObject = {};
  Path path = {};
  const MCState* state = nullptr;
};

PDFExportContext::PDFExportContext(ISize pageSize, PDFDocument* document, const Matrix& transform)
    : _pageSize(pageSize), document(document), _initialTransform(transform) {
  DEBUG_ASSERT(!_pageSize.isEmpty());
  content = MemoryWriteStream::Make();
}

PDFExportContext::~PDFExportContext() = default;

void PDFExportContext::reset() {
  // fGraphicStateResources.reset();
  // fXObjectResources.reset();
  // fShaderResources.reset();
  // fFontResources.reset();
  content.reset();
  // fActiveStackState = SkPDFGraphicStackState();
}

void PDFExportContext::drawFill(const MCState&, const Fill&){};

void PDFExportContext::drawRect(const Rect& rect, const MCState& state, const Fill& fill) {
  Path path;
  path.addRect(rect);
  this->onDrawPath(state, path, fill, true);
}

void PDFExportContext::drawRRect(const RRect&, const MCState&, const Fill&) {
}

void PDFExportContext::drawShape(std::shared_ptr<Shape> shape, const MCState& state,
                                 const Fill& fill) {
  auto path = shape->getPath();
  this->onDrawPath(state, path, fill, false);
}

void PDFExportContext::drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                                 const MCState& state, const Fill& fill) {
  auto rect = Rect::MakeWH(image->width(), image->height());
  onDrawImageRect(image, rect, sampling, state, fill);
}

void PDFExportContext::drawImageRect(std::shared_ptr<Image> image, const Rect& rect,
                                     const SamplingOptions& sampling, const MCState& state,
                                     const Fill& fill) {
  onDrawImageRect(image, rect, sampling, state, fill);
}

void PDFExportContext::drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList,
                                        const MCState& state, const Fill& fill,
                                        const Stroke* stroke) {
  for (const auto& glyphRun : glyphRunList->glyphRuns()) {
    // onDrawGlyphRun(glyphRun, state, fill, stroke);
    onDrawGlyphRunAsPath(glyphRun, state, fill, stroke);
  }
}

void PDFExportContext::onDrawGlyphRunAsPath(const GlyphRun& glyphRun, const MCState& state,
                                            const Fill& fill, const Stroke* stroke) {
  auto glyphFace = glyphRun.glyphFace;
  Path path;

  for (size_t i = 0; i < glyphRun.glyphs.size(); ++i) {
    auto glyphID = glyphRun.glyphs[i];
    auto glyphPosition = glyphRun.positions[i];
    Path glyphPath;
    if (!glyphFace->getPath(glyphID, &glyphPath)) {
      continue;
    }
    glyphPath.transform(Matrix::MakeTrans(glyphPosition.x, glyphPosition.y));
    path.addPath(glyphPath);
  }

  if (path.isEmpty()) {
    return;
  }
  auto shape = Shape::MakeFrom(path);
  shape = Shape::ApplyStroke(std::move(shape), stroke);
  drawShape(shape, state, fill);

  //TODO (YGaurora): maybe hasPerspective()
  Fill transparentFill = fill;
  transparentFill.color = Color::Transparent();
  onDrawGlyphRun(glyphRun, state, transparentFill, stroke);
}

void PDFExportContext::onDrawGlyphRun(const GlyphRun& /*glyphRun*/, const MCState& /*state*/,
                                      const Fill& /*fill*/, const Stroke* /*stroke*/) {
  // const auto& glyphIDs = glyphRun.glyphs;
  // auto glyphFace = glyphRun.glyphFace;

  // if (glyphIDs.empty() || !glyphFace) {
  //   return;
  // }

  // if (fill.maskFilter) {  //this->localToDevice().hasPerspective()
  //   this->onDrawGlyphRunAsPath(glyphRun, state, fill, stroke);
  //   return;
  // }

  // Font glyphRunFont;
  // if (!glyphFace->asFont(&glyphRunFont)) {
  //   return;
  // }
  // auto pdfStrike = PDFStrike::Make(document, glyphRunFont);
  // if (!pdfStrike) {
  //   return;
  // }

  // auto typeface = pdfStrike->strikeSpec.font.getTypeface();

  // const auto* metrics = PDFFont::GetMetrics(typeface, document);
  // if (!metrics) {
  //   return;
  // }

  // const auto& glyphToUnicode = PDFFont::GetUnicodeMap(*typeface, document);

  // // TODO: FontType should probably be on SkPDFStrike?
  // TypefaceMetrics::FontType initialFontType = PDFFont::FontType(*pdfStrike, *metrics);

  // SkClusterator clusterator(glyphRun);

  // // The size, skewX, and scaleX are applied here.
  // float textSize = glyphRunFont.getSize();
  // float advanceScale = textSize * 1.f / pdfStrike->strikeSpec.unitsPerEM;

  // // textScaleX and textScaleY are used to get a conservative bounding box for glyphs.
  // float textScaleY = textSize / pdfStrike->strikeSpec.unitsPerEM;
  // float textScaleX = advanceScale;

  // auto clipStackBounds = state.clip.getBounds();

  // // Clear everything from the runPaint that will be applied by the strike.
  // SkPaint fillPaint(runPaint);
  // if (fillPaint.getStrokeWidth() > 0) {
  //   fillPaint.setStroke(false);
  // }
  // fillPaint.setPathEffect(nullptr);
  // fillPaint.setMaskFilter(nullptr);
  // SkTCopyOnFirstWrite<SkPaint> paint(clean_paint(fillPaint));
  // ScopedContentEntry content(this, *paint, glyphRunFont.getScaleX());
  // if (!content) {
  //   return;
  // }
  // SkDynamicMemoryWStream* out = content.stream();

  // out->writeText("BT\n");
  // SK_AT_SCOPE_EXIT(out->writeText("ET\n"));

  // // Destinations are in absolute coordinates.
  // // The glyphs bounds go through the localToDevice separately for clipping.
  // SkMatrix pageXform = this->deviceToGlobal().asM33();
  // pageXform.postConcat(fDocument->currentPageTransform());

  // ScopedOutputMarkedContentTags mark(fNodeId, {SK_ScalarNaN, SK_ScalarNaN}, fDocument, out);
  // if (!glyphRun.text().empty()) {
  //   fDocument->addNodeTitle(fNodeId, glyphRun.text());
  // }

  // const int numGlyphs = typeface.countGlyphs();

  // if (clusterator.reversedChars()) {
  //   out->writeText("/ReversedChars BMC\n");
  // }
  // SK_AT_SCOPE_EXIT(if (clusterator.reversedChars()) { out->writeText("EMC\n"); });
  // GlyphPositioner glyphPositioner(out, glyphRunFont.getSkewX(), offset);
  // PDFFont* font = nullptr;

  // SkBulkGlyphMetricsAndPaths paths{pdfStrike->fPath.fStrikeSpec};
  // auto glyphs = paths.glyphs(glyphRun.glyphsIDs());

  // while (SkClusterator::Cluster c = clusterator.next()) {
  //   int index = c.fGlyphIndex;
  //   int glyphLimit = index + c.fGlyphCount;

  //   bool actualText = false;
  //   SK_AT_SCOPE_EXIT(if (actualText) {
  //     glyphPositioner.flush();
  //     out->writeText("EMC\n");
  //   });
  //   if (c.fUtf8Text) {  // real cluster
  //     // Check if `/ActualText` needed.
  //     const char* textPtr = c.fUtf8Text;
  //     const char* textEnd = c.fUtf8Text + c.fTextByteLength;
  //     SkUnichar unichar = SkUTF::NextUTF8(&textPtr, textEnd);
  //     if (unichar < 0) {
  //       return;
  //     }
  //     if (textPtr < textEnd ||                                    // >1 code points in cluster
  //         c.fGlyphCount > 1 ||                                    // >1 glyphs in cluster
  //         unichar != map_glyph(glyphToUnicode, glyphIDs[index]))  // 1:1 but wrong mapping
  //     {
  //       glyphPositioner.flush();
  //       out->writeText("/Span<</ActualText ");
  //       SkPDFWriteTextString(out, c.fUtf8Text, c.fTextByteLength);
  //       out->writeText(" >> BDC\n");  // begin marked-content sequence
  //                                     // with an associated property list.
  //       actualText = true;
  //     }
  //   }
  //   for (; index < glyphLimit; ++index) {
  //     SkGlyphID gid = glyphIDs[index];
  //     if (numGlyphs <= gid) {
  //       continue;
  //     }
  //     SkPoint xy = glyphRun.positions()[index];
  //     // Do a glyph-by-glyph bounds-reject if positions are absolute.
  //     SkRect glyphBounds = get_glyph_bounds_device_space(glyphs[index], textScaleX, textScaleY,
  //                                                        xy + offset, this->localToDevice());
  //     if (glyphBounds.isEmpty()) {
  //       if (!contains(clipStackBounds, {glyphBounds.x(), glyphBounds.y()})) {
  //         continue;
  //       }
  //     } else {
  //       if (!clipStackBounds.intersects(glyphBounds)) {
  //         continue;  // reject glyphs as out of bounds
  //       }
  //     }
  //     if (needs_new_font(font, glyphs[index], initialFontType)) {
  //       // Not yet specified font or need to switch font.
  //       font = pdfStrike->getFontResource(glyphs[index]);
  //       SkASSERT(font);  // All preconditions for SkPDFFont::GetFontResource are met.
  //       glyphPositioner.setFont(font);
  //       SkPDFWriteResourceName(out, SkPDFResourceType::kFont,
  //                              add_resource(fFontResources, font->indirectReference()));
  //       out->writeText(" ");
  //       SkPDFUtils::AppendScalar(textSize, out);
  //       out->writeText(" Tf\n");
  //     }
  //     font->noteGlyphUsage(gid);
  //     SkGlyphID encodedGlyph = font->glyphToPDFFontEncoding(gid);
  //     SkScalar advance = advanceScale * glyphs[index]->advanceX();
  //     if (mark) {
  //       SkRect absoluteGlyphBounds = pageXform.mapRect(glyphBounds);
  //       SkPoint& markPoint = mark.point();
  //       if (markPoint.isFinite()) {
  //         markPoint.fX = std::min(absoluteGlyphBounds.fLeft, markPoint.fX);
  //         markPoint.fY = std::max(absoluteGlyphBounds.fBottom, markPoint.fY);  // PDF top
  //       } else {
  //         markPoint = SkPoint{absoluteGlyphBounds.fLeft, absoluteGlyphBounds.fBottom};
  //       }
  //     }
  //     glyphPositioner.writeGlyph(encodedGlyph, advance, xy);
  //   }
  // }
}

void PDFExportContext::drawPicture(std::shared_ptr<Picture> picture, const MCState& state) {
  picture->playback(this, state);
}

namespace {
std::shared_ptr<Record> ModifyRecord(const Record* record, std::shared_ptr<Shader> imageShader) {
  switch (record->type()) {
    case RecordType::DrawFill: {
      const auto* drawFill = static_cast<const DrawFill*>(record);
      auto newDrawFill = std::make_shared<DrawFill>(drawFill->state, drawFill->fill);
      newDrawFill->fill.shader = std::move(imageShader);
      return newDrawFill;
    }
    case RecordType::DrawRect: {
      const auto* drawRect = static_cast<const DrawRect*>(record);
      auto newDrawRect =
          std::make_shared<DrawRect>(drawRect->rect, drawRect->state, drawRect->fill);
      newDrawRect->fill.shader = std::move(imageShader);
      return newDrawRect;
    }
    case RecordType::DrawRRect: {
      const auto* drawRRect = static_cast<const DrawRRect*>(record);
      auto newDrawRRect =
          std::make_shared<DrawRRect>(drawRRect->rRect, drawRRect->state, drawRRect->fill);
      newDrawRRect->fill.shader = std::move(imageShader);
      return newDrawRRect;
    }
    case RecordType::DrawShape: {
      const auto* drawShape = static_cast<const DrawShape*>(record);
      auto newDrawShape =
          std::make_shared<DrawShape>(drawShape->shape, drawShape->state, drawShape->fill);
      newDrawShape->fill.shader = std::move(imageShader);
      return newDrawShape;
    }
    case RecordType::DrawImage: {
      const auto* drawImage = static_cast<const DrawImage*>(record);
      auto newDrawImage = std::make_shared<DrawImage>(drawImage->image, drawImage->sampling,
                                                      drawImage->state, drawImage->fill);
      newDrawImage->fill.shader = std::move(imageShader);
      return newDrawImage;
    }
    case RecordType::DrawImageRect: {
      const auto* drawImageRect = static_cast<const DrawImageRect*>(record);
      auto newDrawImageRect = std::make_shared<DrawImageRect>(
          drawImageRect->image, drawImageRect->rect, drawImageRect->sampling, drawImageRect->state,
          drawImageRect->fill);
      newDrawImageRect->fill.shader = std::move(imageShader);
      return newDrawImageRect;
    }
    case RecordType::DrawGlyphRunList: {
      const auto* drawGlyphRunList = static_cast<const DrawGlyphRunList*>(record);
      auto newDrawGlyphRunList = std::make_shared<DrawGlyphRunList>(
          drawGlyphRunList->glyphRunList, drawGlyphRunList->state, drawGlyphRunList->fill);
      newDrawGlyphRunList->fill.shader = std::move(imageShader);
      return newDrawGlyphRunList;
    }
    case RecordType::DrawLayer: {
      const auto* drawLayer = static_cast<const DrawLayer*>(record);
      auto newDrawLayer = std::make_shared<DrawLayer>(drawLayer->picture, drawLayer->filter,
                                                      drawLayer->state, drawLayer->fill);
      newDrawLayer->fill.shader = std::move(imageShader);
      return newDrawLayer;
    }
    default:
      return nullptr;
  }
}

}  // namespace

void PDFExportContext::drawDropShadowBeforeLayer(const std::shared_ptr<Picture>& picture,
                                                 const DropShadowImageFilter* dropShadowFilter,
                                                 const MCState& state, const Fill& fill) {

  auto pictureBounds = picture->getBounds();
  auto blurBounds = dropShadowFilter->blurFilter->filterBounds(pictureBounds);
  Point offset = {pictureBounds.x() - blurBounds.x(), pictureBounds.y() - blurBounds.y()};

  auto surface = Surface::Make(document->context(), static_cast<int>(blurBounds.width()),
                               static_cast<int>(blurBounds.height()));
  DEBUG_ASSERT(surface);

  auto* canvas = surface->getCanvas();
  canvas->clear(Color::Transparent());

  auto copyFilter = dropShadowFilter->clone();
  copyFilter->dx = 0;
  copyFilter->dy = 0;
  copyFilter->shadowOnly = true;

  Paint picturePaint = {};
  picturePaint.setImageFilter(copyFilter);

  auto matrix = Matrix::MakeTrans(-pictureBounds.x() + offset.x, -pictureBounds.y() + offset.y);
  canvas->drawPicture(picture, &matrix, &picturePaint);

  auto image = surface->makeImageSnapshot();
  if (image) {
    image = image->makeTextureImage(document->context());
    auto imageState = state;
    imageState.matrix.postTranslate(pictureBounds.x() - offset.x + dropShadowFilter->dx,
                                    pictureBounds.y() - offset.y + dropShadowFilter->dy);
    drawImage(std::move(image), SamplingOptions(), imageState, fill);
  }
}

void PDFExportContext::DrawInnerShadowAfterLayer(const std::shared_ptr<Picture>& picture,
                                                 const std::shared_ptr<ImageFilter>& imageFilter,
                                                 const MCState& state, const Fill& fill,
                                                 Context* context,
                                                 PDFExportContext* pdfExportContext) {
  const auto* innerShadowFilter = Caster::AsInnerShadowImageFilter(imageFilter.get());

  auto pictureBounds = picture->getBounds();
  auto surface = Surface::Make(context, static_cast<int>(pictureBounds.width()),
                               static_cast<int>(pictureBounds.height()));
  DEBUG_ASSERT(surface);

  auto* canvas = surface->getCanvas();

  auto copyFilter = innerShadowFilter->clone();
  copyFilter->shadowOnly = true;

  Paint picturePaint = {};
  picturePaint.setImageFilter(copyFilter);

  canvas->translate(-pictureBounds.x(), -pictureBounds.y());
  canvas->drawPicture(picture, &state.matrix, &picturePaint);

  auto image = surface->makeImageSnapshot();
  if (image) {
    image = image->makeTextureImage(context);
    auto imageState = state;
    imageState.matrix.postTranslate(pictureBounds.x(), pictureBounds.y());
    pdfExportContext->drawImage(std::move(image), SamplingOptions(), imageState, fill);
  }
}

void PDFExportContext::drawInnerShadowAfterLayer(const Record* record,
                                                 const InnerShadowImageFilter* innerShadowFilter,
                                                 const MCState& state) {
  MeasureContext measureContext;
  record->playback(&measureContext);
  auto pictureBounds = measureContext.getBounds();

  auto surface = Surface::Make(document->context(), static_cast<int>(pictureBounds.width()),
                               static_cast<int>(pictureBounds.height()));
  DEBUG_ASSERT(surface);
  auto* canvas = surface->getCanvas();
  canvas->translate(-pictureBounds.x(), -pictureBounds.y());

  auto copyFilter = innerShadowFilter->clone();
  copyFilter->shadowOnly = true;
  Paint picturePaint = {};
  picturePaint.setImageFilter(copyFilter);

  canvas->saveLayer(&picturePaint);
  {
    auto* surfaceContext = canvas->drawContext;
    auto matrix = Matrix::I();
    matrix.postTranslate(-pictureBounds.x(), -pictureBounds.y());
    auto transformContext = TransformContext::Make(surfaceContext, matrix, state.clip);
    if (transformContext) {
      surfaceContext = transformContext.get();
    }
    record->playback(surfaceContext);
  }
  canvas->restore();

  auto image = surface->makeImageSnapshot();
  auto imageShader = Shader::MakeImageShader(image);
  imageShader =
      imageShader->makeWithMatrix(Matrix::MakeTrans(pictureBounds.x(), pictureBounds.y()));
  if (auto imageRecord = ModifyRecord(record, imageShader)) {
    DrawContext* drawContext = this;
    auto matrix = state.matrix;
    // matrix.postTranslate(pictureBounds.x(), pictureBounds.y());
    auto transformContext = TransformContext::Make(drawContext, matrix, state.clip);
    if (transformContext) {
      drawContext = transformContext.get();
    } else if (state.clip.isEmpty() && !state.clip.isInverseFillType()) {
      return;
    }
    imageRecord->playback(drawContext);
  }

  // if (image) {
  //   image = image->makeTextureImage(document->context());
  //   auto imageState = state;
  //   imageState.matrix.postTranslate(pictureBounds.x(), pictureBounds.y());
  //   drawImage(std::move(image), SamplingOptions(), imageState, Fill());
  // }
}

void PDFExportContext::drawBlurLayer(const std::shared_ptr<Picture>& picture,
                                     const std::shared_ptr<ImageFilter>& imageFilter,
                                     const MCState& state, const Fill& fill) {
  auto pictureBounds = picture->getBounds();
  auto blurBounds = imageFilter->filterBounds(pictureBounds);
  blurBounds = blurBounds.makeOutset(100, 100);
  Point offset = {pictureBounds.x() - blurBounds.x(), pictureBounds.y() - blurBounds.y()};

  auto surface = Surface::Make(document->context(), static_cast<int>(blurBounds.width()),
                               static_cast<int>(blurBounds.height()));
  DEBUG_ASSERT(surface);

  Canvas* canvas = surface->getCanvas();
  canvas->clear(Color::Transparent());

  Paint picturePaint = {};
  picturePaint.setImageFilter(imageFilter);

  auto matrix = state.matrix;
  matrix.postTranslate(-pictureBounds.x() + offset.x, -pictureBounds.y() + offset.y);
  canvas->drawPicture(picture, &matrix, &picturePaint);

  auto image = surface->makeImageSnapshot();
  if (image) {
    image = image->makeTextureImage(document->context());
    auto imageState = state;
    imageState.matrix.postTranslate(pictureBounds.x() - offset.x, pictureBounds.y() - offset.y);
    drawImage(std::move(image), SamplingOptions(), imageState, fill);
  }
}

void PDFExportContext::drawLayer(std::shared_ptr<Picture> picture,
                                 std::shared_ptr<ImageFilter> imageFilter, const MCState& state,
                                 const Fill& fill) {

  if (const auto* dropShadowFilter = Caster::AsDropShadowImageFilter(imageFilter.get())) {
    drawDropShadowBeforeLayer(picture, dropShadowFilter, state, fill);
    if (!dropShadowFilter->shadowOnly) {
      picture->playback(this, state);
    }
    return;
  }
  if (const auto* innerShadowFilter = Caster::AsInnerShadowImageFilter(imageFilter.get())) {
    auto records = picture->records;
    for (auto* record : records) {
      record->playback(this);
      drawInnerShadowAfterLayer(record, innerShadowFilter, state);
    }
    return;
  }
  if (const auto* blurShadowFilter = Caster::AsBlurImageFilter(imageFilter.get())) {
    drawBlurLayer(picture, imageFilter, state, fill);
    return;
  }
  picture->playback(this, state);
}

std::shared_ptr<Data> PDFExportContext::getContent() {
  // if (fActiveStackState.fContentStream) {
  //   fActiveStackState.drainStack();
  //   fActiveStackState = SkPDFGraphicStackState();
  // }
  if (content->bytesWritten() == 0) {
    return Data::MakeEmpty();
  }
  auto buffer = MemoryWriteStream::Make();
  if (!_initialTransform.isIdentity()) {
    PDFUtils::AppendTransform(_initialTransform, buffer);
  }
  if (needsExtraSave) {
    buffer->writeText("q\n");
  }
  content->writeToAndReset(buffer);
  if (needsExtraSave) {
    buffer->writeText("Q\n");
  }
  needsExtraSave = false;
  return buffer->readData();
}

void PDFExportContext::onDrawPath(const MCState& state, const Path& path, const Fill& fill,
                                  bool /*pathIsMutable*/) {
  // if (state.clip.isEmpty()) {
  //   return;
  // }
  // Paint paint(clean_paint(srcPaint));
  Path modifiedPath;
  const Path* pathPointer = &path;

  if (fill.maskFilter) {
    this->drawPathWithFilter(state, path, Matrix::I(), fill);
    return;
  }

  Matrix matrix = Matrix::I();
  // if (paint->getPathEffect()) {
  //   if (clipStack.isEmpty(this->bounds())) {
  //     return;
  //   }
  //   if (!pathIsMutable) {
  //     modifiedPath = origPath;
  //     pathPtr = &modifiedPath;
  //     pathIsMutable = true;
  //   }
  //   if (skpathutils::FillPathWithPaint(*pathPtr, *paint, pathPtr)) {
  //     set_style(&paint, SkPaint::kFill_Style);
  //   } else {
  //     set_style(&paint, SkPaint::kStroke_Style);
  //     if (paint->getStrokeWidth() != 0) {
  //       paint.writable()->setStrokeWidth(0);
  //     }
  //   }
  //   paint.writable()->setPathEffect(nullptr);
  // }

  // if (this->handleInversePath(*pathPtr, *paint, pathIsMutable)) {
  //   return;
  // }
  // if (matrix.getType() & SkMatrix::kPerspective_Mask) {
  //   if (!pathIsMutable) {
  //     modifiedPath = origPath;
  //     pathPtr = &modifiedPath;
  //     pathIsMutable = true;
  //   }
  //   pathPtr->transform(matrix);
  //   if (paint->getShader()) {
  //     transform_shader(paint.writable(), matrix);
  //   }
  //   matrix = SkMatrix::I();
  // }

  ScopedContentEntry scopedContent(this, &state, matrix, fill);
  if (!scopedContent) {
    return;
  }

  constexpr float ToleranceScale = 0.0625f;  // smaller = better conics (circles).
  Point scalePoint{0, 0};
  matrix.mapXY(1.0f, 1.0f, &scalePoint);
  float matrixScale = scalePoint.length();
  float tolerance = matrixScale > 0.0f ? ToleranceScale / matrixScale : ToleranceScale;
  // bool consumeDegeratePathSegments =
  //     paint->getStyle() == SkPaint::kFill_Style || (paint->getStrokeCap() != SkPaint::kRound_Cap &&
  //                                                   paint->getStrokeCap() != SkPaint::kSquare_Cap);

  PDFUtils::EmitPath(*pathPointer, false, content, tolerance);
  PDFUtils::PaintPath(pathPointer->getFillType(), content);
}

namespace {
// std::shared_ptr<Image> alpha_image_to_greyscale_image(const Image* mask) {
//   int w = mask->width();
//   int h = mask->height();
//   Bitmap greyBitmap;
//   greyBitmap.allocPixels(SkImageInfo::Make(w, h, kGray_8_SkColorType, kOpaque_SkAlphaType));
//   // TODO: support gpu images in pdf
//   if (!mask->readPixels(nullptr, SkImageInfo::MakeA8(w, h), greyBitmap.getPixels(),
//                         greyBitmap.rowBytes(), 0, 0)) {
//     return nullptr;
//   }
//   greyBitmap.setImmutable();
//   return greyBitmap.asImage();
// }

// std::shared_ptr<Image> mask_to_grayscale_image(MaskBuilder* mask) {
//   sk_sp<SkImage> img;
//   SkPixmap pm(SkImageInfo::Make(mask->fBounds.width(), mask->fBounds.height(), kGray_8_SkColorType,
//                                 kOpaque_SkAlphaType),
//               mask->fImage, mask->fRowBytes);
//   const int imgQuality = SK_PDF_MASK_QUALITY;
//   if (imgQuality <= 100 && imgQuality >= 0) {
//     SkDynamicMemoryWStream buffer;
//     SkJpegEncoder::Options jpegOptions;
//     jpegOptions.fQuality = imgQuality;
//     if (SkJpegEncoder::Encode(&buffer, pm, jpegOptions)) {
//       img = SkImages::DeferredFromEncodedData(buffer.detachAsData());
//       SkASSERT(img);
//       if (img) {
//         SkMaskBuilder::FreeImage(mask->image());
//       }
//     }
//   }
//   if (!img) {
//     img = SkImages::RasterFromPixmap(
//         pm, [](const void* p, void*) { SkMaskBuilder::FreeImage(const_cast<void*>(p)); }, nullptr);
//   }
//   *mask = SkMaskBuilder();  // destructive;
//   return img;
// }
}

void PDFExportContext::onDrawImageRect(std::shared_ptr<Image> image, const Rect& rect,
                                       const SamplingOptions& sampling, const MCState& state,
                                       const Fill& fill) {
  // if (this->hasEmptyClip()) {
  //   return;
  // }
  if (!image) {
    return;
  }

  // First, figure out the src->dst transform and subset the image if needed.
  auto bound = Rect::MakeWH(image->width(), image->height());
  float scaleX = rect.width() / bound.width();
  float scaleY = rect.height() / bound.height();
  float transX = rect.left - bound.left * scaleX;
  float transY = rect.top - bound.top * scaleY;
  auto transform = Matrix::I();
  transform.postScale(scaleX, scaleY);
  transform.postTranslate(transX, transY);

  // if (style.blendMode != BlendMode::SrcOver && image. imageSubset.image()->isOpaque() &&
  //     SkBlendFastPath::kSrcOver == CheckFastPath(*paint, false)) {
  //   paint.writable()->setBlendMode(SkBlendMode::kSrcOver);
  // }

  // Alpha-only images need to get their color from the shader, before
  // applying the colorfilter.
  auto modifiedFill = fill;
  if (image->isAlphaOnly() && modifiedFill.colorFilter) {
    // must blend alpha image and shader before applying colorfilter.
    auto surface = Surface::Make(document->context(), image->width(), image->height());
    Canvas* canvas = surface->getCanvas();
    Paint tmpPaint;
    // In the case of alpha images with shaders, the shader's coordinate
    // system is the image's coordiantes.
    tmpPaint.setShader(modifiedFill.shader);
    tmpPaint.setColor(modifiedFill.color);
    canvas->clear();
    canvas->drawImage(image, &tmpPaint);
    if (modifiedFill.shader != nullptr) {
      modifiedFill.shader = nullptr;
    }
    image = surface->makeImageSnapshot();
    DEBUG_ASSERT(!image->isAlphaOnly());
  }

  if (image->isAlphaOnly()) {
    // The ColorFilter applies to the paint color/shader, not the alpha layer.
    DEBUG_ASSERT(modifiedFill.colorFilter == nullptr);

    // sk_sp<SkImage> mask = alpha_image_to_greyscale_image(imageSubset.image().get());
    // if (!mask) {
    //   return;
    // }

    // PDF doesn't seem to allow masking vector graphics with an Image XObject.
    // Must mask with a Form XObject.
    auto maskContext = PDFExportContext(ISize::Make(image->width(), image->height()), document);
    {
      auto canvas = PDFDocument::MakeCanvas(&maskContext);
      // This clip prevents the mask image shader from covering
      // entire device if unnecessary.
      canvas->clipRect(state.clip.getBounds());
      // canvas.concat(ctm);
      if (modifiedFill.maskFilter) {
        Paint tmpPaint;
        auto imageShader =
            Shader::MakeImageShader(image, TileMode::Clamp, TileMode::Clamp, SamplingOptions());
        imageShader = imageShader->makeWithMatrix(transform);
        tmpPaint.setShader(imageShader);
        tmpPaint.setMaskFilter(modifiedFill.maskFilter);
        canvas->drawRect(rect, tmpPaint);
      } else {
        // if (src && !is_integral(*src)) {
        //   canvas.clipRect(dst);
        // }
        canvas->concat(transform);
        canvas->drawImage(image, sampling);
      }
    }
    // SkIRect maskDeviceBounds = maskDevice->cs().bounds(maskDevice->bounds()).roundOut();
    auto maskDeviceBounds = Rect::MakeSize(maskContext.pageSize());
    // if (!ctm.isIdentity() && paint->getShader()) {
    //   transform_shader(paint.writable(), ctm);  // Since we are using identity matrix.
    // }
    ScopedContentEntry content(this, &state, Matrix::I(), modifiedFill);
    if (!content) {
      return;
    }
    auto xObjext = maskContext.makeFormXObjectFromDevice(maskDeviceBounds, true);
    auto graphicState = PDFGraphicState::GetSMaskGraphicState(
        xObjext, false, PDFGraphicState::SMaskMode::Luminosity, document);
    this->setGraphicState(graphicState, content.stream());
    PDFUtils::AppendRectangle(Rect::MakeSize(_pageSize), content.stream());
    PDFUtils::PaintPath(PathFillType::Winding, content.stream());
    this->clearMaskOnGraphicState(content.stream());
    return;
  }
  if (modifiedFill.maskFilter) {
    auto imageShader =
        Shader::MakeImageShader(image, TileMode::Clamp, TileMode::Clamp, SamplingOptions());
    imageShader = imageShader->makeWithMatrix(transform);
    modifiedFill.shader = imageShader;

    Path path;
    path.addRect(rect);
    this->onDrawPath(state, path, modifiedFill, true);
    return;
  }
  // transform.postConcat(ctm);

  // bool needToRestore = false;
  // if (src && !is_integral(*src)) {
  //   // Need sub-pixel clipping to fix https://bug.skia.org/4374
  //   this->cs().save();
  //   this->cs().clipRect(dst, ctm, SkClipOp::kIntersect, true);
  //   needToRestore = true;
  // }
  // SK_AT_SCOPE_EXIT(if (needToRestore) { this->cs().restore(); });

  auto matrix = transform;

  // Rasterize the bitmap using perspective in a new bitmap.
  if (false) {  //transform.hasPerspective()
    // Transform the bitmap in the new space, without taking into
    // account the initial transform.
    auto imageBounds = Rect::MakeWH(image->width(), image->height());
    Path perspectiveOutline;
    perspectiveOutline.addRect(imageBounds);
    perspectiveOutline.transform(matrix);

    // Retrieve the bounds of the new shape.
    auto outlineBounds = perspectiveOutline.getBounds();
    if (!outlineBounds.intersect(state.clip.getBounds())) {
      return;
    }

    // Transform the bitmap in the new space to the final space, to account for DPI
    auto physicalBounds = _initialTransform.mapRect(outlineBounds);
    float scaleX = physicalBounds.width() / outlineBounds.width();
    float scaleY = physicalBounds.height() / outlineBounds.height();

    // TODO(edisonn): A better approach would be to use a bitmap shader
    // (in clamp mode) and draw a rect over the entire bounding box. Then
    // intersect perspectiveOutline to the clip. That will avoid introducing
    // alpha to the image while still giving good behavior at the edge of
    // the image.  Avoiding alpha will reduce the pdf size and generation
    // CPU time some.
    auto surface =
        Surface::Make(document->context(), static_cast<int>(std::ceil(physicalBounds.width())),
                      static_cast<int>(std::ceil(physicalBounds.height())));
    if (!surface) {
      return;
    }
    Canvas* canvas = surface->getCanvas();
    canvas->clear();

    float deltaX = outlineBounds.left;
    float deltaY = outlineBounds.top;

    auto offsetMatrix = transform;
    offsetMatrix.postTranslate(-deltaX, -deltaY);
    offsetMatrix.postScale(scaleX, scaleY);

    // Translate the draw in the new canvas, so we perfectly fit the
    // shape in the bitmap.
    canvas->setMatrix(offsetMatrix);
    canvas->drawImage(image, 0, 0);

    // In the new space, we use the identity matrix translated
    // and scaled to reflect DPI.
    matrix.setScale(1 / scaleX, 1 / scaleY);
    matrix.postTranslate(deltaX, deltaY);

    image = surface->makeImageSnapshot();
    if (!image) {
      return;
    }
  }

  Matrix scaled;
  // Adjust for origin flip.
  scaled.setScale(1.f, -1.f);
  scaled.postTranslate(0, 1.f);
  // Scale the image up from 1x1 to WxH.
  auto subset = Rect::MakeWH(image->width(), image->height());
  scaled.postScale(subset.width(), subset.height());
  scaled.postConcat(matrix);
  ScopedContentEntry content(this, &state, scaled, modifiedFill);
  if (!content) {
    return;
  }
  Path shape;
  shape.addRect(subset);
  shape.transform(matrix);
  if (content.needShape()) {
    content.setShape(shape);
  }
  if (!content.needSource()) {
    return;
  }

  if (auto colorFilter = modifiedFill.colorFilter) {
    auto imageFilter = ImageFilter::ColorFilter(colorFilter);
    image = image->makeWithFilter(imageFilter);
    if (!image) {
      return;
    }
    // TODO(halcanary): de-dupe this by caching filtered images.
    // (maybe in the resource cache?)
  }

  // SkBitmapKey key = imageSubset.key();
  // SkPDFIndirectReference* pdfimagePtr = fDocument->fPDFBitmapMap.find(key);
  // SkPDFIndirectReference pdfimage = pdfimagePtr ? *pdfimagePtr : SkPDFIndirectReference();
  // if (!pdfimagePtr) {
  //   SkASSERT(imageSubset);
  //   pdfimage = SkPDFSerializeImage(imageSubset.image().get(), fDocument,
  //                                  fDocument->metadata().fEncodingQuality);
  //   SkASSERT((key != SkBitmapKey{{0, 0, 0, 0}, 0}));
  //   fDocument->fPDFBitmapMap.set(key, pdfimage);
  // }
  // SkASSERT(pdfimage != SkPDFIndirectReference());
  // this->drawFormXObject(pdfimage, content.stream(), &shape);

  PDFIndirectReference pdfImage =
      PDFBitmap::Serialize(image, document, document->metadata().encodingQuality);
  this->drawFormXObject(pdfImage, content.stream(), &shape);
}

namespace {
std::vector<PDFIndirectReference> sort(const std::unordered_set<PDFIndirectReference>& src) {
  std::vector<PDFIndirectReference> dst;
  dst.reserve(src.size());
  for (auto ref : src) {
    dst.push_back(ref);
  }
  std::sort(dst.begin(), dst.end(),
            [](PDFIndirectReference a, PDFIndirectReference b) { return a.fValue < b.fValue; });
  return dst;
}
}  // namespace

std::unique_ptr<PDFDictionary> PDFExportContext::makeResourceDict() {
  return MakePDFResourceDictionary(sort(fGraphicStateResources), sort(fShaderResources),
                                   sort(fXObjectResources), sort(fFontResources));
}

std::unique_ptr<PDFDictionary> PDFExportContext::makeResourceDictionary() {
  return MakePDFResourceDictionary(sort(fGraphicStateResources), sort(fShaderResources),
                                   sort(fXObjectResources), sort(fFontResources));
}

bool PDFExportContext::isContentEmpty() {
  return content->bytesWritten() == 0 && contentBuffer->bytesWritten() == 0;
}

PDFIndirectReference PDFExportContext::makeFormXObjectFromDevice(Rect bounds, bool alpha) {
  auto inverseTransform = Matrix::I();
  if (!_initialTransform.isIdentity()) {
    if (!_initialTransform.invert(&inverseTransform)) {
      LOGE("Layer initial transform should be invertible.");
      inverseTransform.reset();
    }
  }

  const char* colorSpace = alpha ? "DeviceGray" : nullptr;

  auto mediaBox = MakePDFArray(static_cast<int>(bounds.left), static_cast<int>(bounds.top),
                               static_cast<int>(bounds.right), static_cast<int>(bounds.bottom));
  PDFIndirectReference xObject =
      MakePDFFormXObject(document, getContent(), std::move(mediaBox), makeResourceDictionary(),
                         inverseTransform, colorSpace);

  reset();
  return xObject;
}

PDFIndirectReference PDFExportContext::makeFormXObjectFromDevice(bool alpha) {
  return makeFormXObjectFromDevice(
      Rect{0, 0, static_cast<float>(_pageSize.width), static_cast<float>(_pageSize.height)}, alpha);
}

namespace {

int add_resource(std::unordered_set<PDFIndirectReference>& resources, PDFIndirectReference ref) {
  resources.insert(ref);
  return ref.fValue;
}

bool treat_as_regular_pdf_blend_mode(BlendMode blendMode) {
  return PDFUtils::BlendModeName(blendMode) != nullptr;
}

void populate_graphic_state_entry_from_paint(
    PDFDocument* document, const Matrix& matrix, const MCState* state, Rect deviceBounds,
    const Fill& fill, const Matrix& initialTransform, float textScale,
    PDFGraphicStackState::Entry* entry, std::unordered_set<PDFIndirectReference>* shaderResources,
    std::unordered_set<PDFIndirectReference>* graphicStateResources) {

  // NOT_IMPLEMENTED(paint.getPathEffect() != nullptr, false);
  // NOT_IMPLEMENTED(paint.getMaskFilter() != nullptr, false);
  // NOT_IMPLEMENTED(paint.getColorFilter() != nullptr, false);

  entry->fMatrix = state->matrix * matrix;
  // entry->fClipStackGenID = clipStack ? clipStack->getTopmostGenID() : SkClipStack::kWideOpenGenID;
  auto color = fill.color;
  color.alpha = 1;
  entry->fColor = color;
  entry->fShaderIndex = -1;

  // PDF treats a shader as a color, so we only set one or the other.
  if (auto shader = fill.shader) {
    // note: we always present the alpha as 1 for the shader, knowing that it will be
    //       accounted for when we create our newGraphicsState (below)
    if (const auto* colorShader = Caster::AsColorShader(shader.get())) {
      if (colorShader->asColor(&color)) {
        color.alpha = 1;
        entry->fColor = color;
      }
    } else {
      // PDF positions patterns relative to the initial transform, so
      // we need to apply the current transform to the shader parameters.
      auto transform = matrix;
      transform.postConcat(initialTransform);

      // PDF doesn't support kClamp_TileMode, so we simulate it by making
      // a pattern the size of the current clip.

      // Rect clipStackBounds =
      //     clipStack ? clipStack->bounds(deviceBounds) : SkRect::Make(deviceBounds);
      Rect clipStackBounds = deviceBounds;

      // We need to apply the initial transform to bounds in order to get
      // bounds in a consistent coordinate system.
      initialTransform.mapRect(&clipStackBounds);
      clipStackBounds.roundOut();

      PDFIndirectReference pdfShader =
          PDFShader::Make(document, shader, transform, clipStackBounds, color);
      if (pdfShader) {
        // pdfShader has been canonicalized so we can directly compare pointers.
        entry->fShaderIndex = add_resource(*shaderResources, pdfShader);
      }
    }
  }

  PDFIndirectReference newGraphicState;
  if (color == fill.color) {
    newGraphicState = PDFGraphicState::GetGraphicStateForPaint(document, fill);
  } else {
    auto newPaint = fill;
    newPaint.color = color;
    newGraphicState = PDFGraphicState::GetGraphicStateForPaint(document, newPaint);
  }
  entry->fGraphicStateIndex = add_resource(*graphicStateResources, newGraphicState);
  entry->fTextScaleX = textScale;
}

}  // namespace

std::shared_ptr<MemoryWriteStream> PDFExportContext::setUpContentEntry(
    const MCState* state, const Matrix& matrix, const Fill& fill, float scale,
    PDFIndirectReference* destination) {
  DEBUG_ASSERT(!*destination);
  BlendMode blendMode = fill.blendMode;

  if (blendMode == BlendMode::Dst) {
    return nullptr;
  }

  if (!treat_as_regular_pdf_blend_mode(blendMode) && blendMode != BlendMode::DstOver) {
    if (!isContentEmpty()) {
      *destination = this->makeFormXObjectFromDevice();
      DEBUG_ASSERT(isContentEmpty());
    } else if (blendMode != BlendMode::Src && blendMode != BlendMode::SrcOut) {
      return nullptr;
    }
  }

  if (treat_as_regular_pdf_blend_mode(blendMode)) {
    if (!fActiveStackState.fContentStream) {
      if (content->bytesWritten() != 0) {
        content->writeText("Q\nq\n");
        needsExtraSave = true;
      }
      fActiveStackState = PDFGraphicStackState(content);
    } else {
      DEBUG_ASSERT(fActiveStackState.fContentStream == content);
    }
  } else {
    fActiveStackState.drainStack();
    fActiveStackState = PDFGraphicStackState(contentBuffer);
  }

  DEBUG_ASSERT(fActiveStackState.fContentStream);
  PDFGraphicStackState::Entry entry;
  populate_graphic_state_entry_from_paint(document, matrix, state, Rect::MakeSize(_pageSize), fill,
                                          _initialTransform, scale, &entry, &fShaderResources,
                                          &fGraphicStateResources);
  fActiveStackState.updateClip(*state, Rect::MakeSize(_pageSize));
  fActiveStackState.updateMatrix(entry.fMatrix);
  fActiveStackState.updateDrawingState(entry);

  return fActiveStackState.fContentStream;
}

void PDFExportContext::finishContentEntry(const MCState* state, BlendMode blendMode,
                                          PDFIndirectReference destination, Path* path) {
  DEBUG_ASSERT(blendMode != BlendMode::Dst);
  if (treat_as_regular_pdf_blend_mode(blendMode)) {
    DEBUG_ASSERT(!destination);
    return;
  }

  DEBUG_ASSERT(fActiveStackState.fContentStream);

  fActiveStackState.drainStack();
  fActiveStackState = PDFGraphicStackState();

  if (blendMode == BlendMode::DstOver) {
    DEBUG_ASSERT(!destination);
    if (contentBuffer->bytesWritten() != 0) {
      if (content->bytesWritten() != 0) {
        contentBuffer->writeText("Q\nq\n");
        needsExtraSave = true;
      }
      contentBuffer->prependToAndReset(content);
      DEBUG_ASSERT(contentBuffer->bytesWritten() == 0);
    }
    return;
  }
  if (contentBuffer->bytesWritten() != 0) {
    if (content->bytesWritten() != 0) {
      content->writeText("Q\nq\n");
      needsExtraSave = true;
    }
    contentBuffer->writeToAndReset(content);
    DEBUG_ASSERT(contentBuffer->bytesWritten() == 0);
  }

  if (!destination) {
    DEBUG_ASSERT((blendMode == BlendMode::Src || blendMode == BlendMode::SrcOut));
    return;
  }

  DEBUG_ASSERT(destination);
  // Changing the current content into a form-xobject will destroy the clip
  // objects which is fine since the xobject will already be clipped. However
  // if source has shape, we need to clip it too, so a copy of the clip is
  // saved.

  Fill stockPaint;

  PDFIndirectReference srcFormXObject;
  if (this->isContentEmpty()) {
    // If nothing was drawn and there's no shape, then the draw was a
    // no-op, but dst needs to be restored for that to be true.
    // If there is shape, then an empty source with Src, SrcIn, SrcOut,
    // DstIn, DstAtop or Modulate reduces to Clear and DstOut or SrcAtop
    // reduces to Dst.
    if (path == nullptr || blendMode == BlendMode::DstOut || blendMode == BlendMode::SrcATop) {
      ScopedContentEntry contentEntry(this, nullptr, Matrix::I(), stockPaint);
      this->drawFormXObject(destination, contentEntry.stream(), nullptr);
      return;
    } else {
      blendMode = BlendMode::Clear;
    }
  } else {
    srcFormXObject = this->makeFormXObjectFromDevice();
  }

  PDFIndirectReference xObject;
  PDFIndirectReference sMask;
  if (blendMode == BlendMode::SrcATop) {
    xObject = srcFormXObject;
    sMask = destination;
  } else {
    if (path != nullptr) {
      // Draw shape into a form-xobject.
      Fill filledPaint;
      filledPaint.color = Color::Black();
      MCState empty;
      PDFExportContext shapeContext(_pageSize, document, _initialTransform);
      shapeContext.onDrawPath(state ? *state : empty, *path, filledPaint, true);
      xObject = destination;
      sMask = shapeContext.makeFormXObjectFromDevice();
    } else {
      xObject = destination;
      sMask = srcFormXObject;
    }
  }
  this->drawFormXObjectWithMask(xObject, sMask, BlendMode::SrcOver, true);

  if (blendMode == BlendMode::Clear) {
    return;
  } else if (blendMode == BlendMode::Src || blendMode == BlendMode::DstATop) {
    ScopedContentEntry content(this, nullptr, Matrix::I(), stockPaint);
    if (content) {
      this->drawFormXObject(srcFormXObject, content.stream(), nullptr);
    }
    if (blendMode == BlendMode::Src) {
      return;
    }
  } else if (blendMode == BlendMode::SrcATop) {
    ScopedContentEntry content(this, nullptr, Matrix::I(), stockPaint);
    if (content) {
      this->drawFormXObject(destination, content.stream(), nullptr);
    }
  }

  DEBUG_ASSERT((blendMode == BlendMode::SrcIn || blendMode == BlendMode::DstIn ||
                blendMode == BlendMode::SrcOut || blendMode == BlendMode::DstOut ||
                blendMode == BlendMode::SrcATop || blendMode == BlendMode::DstATop ||
                blendMode == BlendMode::Modulate));

  if (blendMode == BlendMode::SrcIn || blendMode == BlendMode::SrcOut ||
      blendMode == BlendMode::SrcATop) {
    this->drawFormXObjectWithMask(srcFormXObject, destination, BlendMode::SrcOver,
                                  blendMode == BlendMode::SrcOut);
    return;
  } else {
    auto mode = BlendMode::SrcOver;
    if (blendMode == BlendMode::Modulate) {
      this->drawFormXObjectWithMask(srcFormXObject, destination, BlendMode::SrcOver, false);
      mode = BlendMode::Multiply;
    }
    this->drawFormXObjectWithMask(destination, srcFormXObject, mode,
                                  blendMode == BlendMode::DstOut);
    return;
  }
}

void PDFExportContext::drawFormXObject(PDFIndirectReference xObject,
                                       std::shared_ptr<MemoryWriteStream> stream, Path* shape) {
  auto point = Point::Zero();
  if (shape) {
    // Destinations are in absolute coordinates.
    // Matrix pageXform = this->deviceToGlobal().asM33();
    // pageXform.postConcat(document->currentPageTransform());
    auto pageXform = document->currentPageTransform();
    // The shape already has localToDevice applied.

    auto shapeBounds = shape->getBounds();
    pageXform.mapRect(&shapeBounds);
    point = Point{shapeBounds.left, shapeBounds.bottom};
  }

  // ScopedOutputMarkedContentTags mark(fNodeId, point, document, stream);

  DEBUG_ASSERT(xObject);
  PDFWriteResourceName(stream, PDFResourceType::XObject, add_resource(fXObjectResources, xObject));
  content->writeText(" Do\n");
}

void PDFExportContext::clearMaskOnGraphicState(const std::shared_ptr<MemoryWriteStream>& stream) {
  auto& noSMaskGS = document->fNoSmaskGraphicState;
  if (!noSMaskGS) {
    auto tmp = PDFDictionary::Make("ExtGState");
    tmp->insertName("SMask", "None");
    noSMaskGS = document->emit(*tmp);
  }
  this->setGraphicState(noSMaskGS, stream);
}

void PDFExportContext::setGraphicState(PDFIndirectReference graphicState,
                                       const std::shared_ptr<MemoryWriteStream>& stream) {
  PDFUtils::ApplyGraphicState(add_resource(fGraphicStateResources, graphicState), stream);
}

void PDFExportContext::drawFormXObjectWithMask(PDFIndirectReference xObject,
                                               PDFIndirectReference sMask, BlendMode mode,
                                               bool invertClip) {
  DEBUG_ASSERT(sMask);
  Fill fill;
  fill.blendMode = mode;
  ScopedContentEntry content(this, nullptr, Matrix::I(), fill);
  if (!content) {
    return;
  }

  this->setGraphicState(PDFGraphicState::GetSMaskGraphicState(
                            sMask, invertClip, PDFGraphicState::SMaskMode::Alpha, document),
                        content.stream());
  this->drawFormXObject(xObject, content.stream(), nullptr);
  this->clearMaskOnGraphicState(content.stream());
}

namespace {

std::tuple<std::shared_ptr<Picture>, Matrix> MaskFilterToPicture(
    const ShaderMaskFilter* shaderMaskFilter) {
  auto matrix = Matrix::I();
  auto shader = shaderMaskFilter->getShader();
  while (true) {
    if (const auto* matrixShader = Caster::AsMatrixShader(shader.get())) {
      matrix.postConcat(matrixShader->matrix);
      shader = matrixShader->source;
    } else if (const auto* imageShader = Caster::AsImageShader(shader.get())) {
      if (const auto* pictureImage = Caster::AsPictureImage(imageShader->image.get())) {
        return {pictureImage->picture, matrix};
      }
      return {nullptr, Matrix::I()};
    } else {
      return {nullptr, Matrix::I()};
    }
  }
};

}  // namespace

void PDFExportContext::drawPathWithFilter(const MCState& state, const Path& originPath,
                                          const Matrix& matrix, const Fill& originPaint) {
  DEBUG_ASSERT(originPaint.maskFilter);

  Path path(originPath);
  path.transform(matrix);
  auto maskBound = path.getBounds();

  Fill paint(originPaint);

  const auto* shaderMaskFilter = Caster::AsShaderMaskFilter(originPaint.maskFilter.get());
  auto [picture, pictureMatrix] = MaskFilterToPicture(shaderMaskFilter);

  auto maskContext = makeCongruentDevice();
  if (!picture) {  //mask as image
    auto surface = Surface::Make(document->context(), static_cast<int>(maskBound.width()),
                                 static_cast<int>(maskBound.height()));
    Canvas* maskCanvas = surface->getCanvas();
    Paint maskPaint;
    maskPaint.setShader(shaderMaskFilter->getShader());
    maskCanvas->drawPaint(maskPaint);

    auto grayscaleInfo = ImageInfo::Make(surface->width(), surface->height(), ColorType::ALPHA_8);
    auto byteSize = grayscaleInfo.byteSize();
    void* pixels = malloc(byteSize);
    if (!surface->readPixels(grayscaleInfo, pixels)) {
      free(pixels);
      return;
    }
    auto pixelData = Data::MakeAdopted(pixels, byteSize, Data::FreeProc);
    // Convert alpha-8 to a grayscale image
    grayscaleInfo = ImageInfo::Make(surface->width(), surface->height(), ColorType::Gray_8);
    auto maskImage = Image::MakeFrom(grayscaleInfo, pixelData);

    // PDF doesn't seem to allow masking vector graphics with an Image XObject.
    // Must mask with a Form XObject.
    {
      Canvas canvas(maskContext.get());
      canvas.drawImage(maskImage, maskBound.x(), maskBound.y());
    }
  } else {
    Canvas canvas(maskContext.get());
    canvas.concat(pictureMatrix);
    canvas.drawPicture(picture);
  }

  if (!state.matrix.isIdentity() && paint.shader) {
    paint.shader = paint.shader->makeWithMatrix(matrix);
  }
  ScopedContentEntry contentEntry(this, &state, Matrix::I(), paint);
  if (!contentEntry) {
    return;
  }

  setGraphicState(
      PDFGraphicState::GetSMaskGraphicState(
          maskContext->makeFormXObjectFromDevice(maskBound, true), false,
          picture ? PDFGraphicState::SMaskMode::Alpha : PDFGraphicState::SMaskMode::Luminosity,
          document),
      contentEntry.stream());
  // PDFUtils::AppendRectangle(maskBound, contentEntry.stream());
  {
    constexpr float ToleranceScale = 0.0625f;  // smaller = better conics (circles).
    Point scalePoint{0, 0};
    matrix.mapXY(1.0f, 1.0f, &scalePoint);
    float matrixScale = scalePoint.length();
    float tolerance = matrixScale > 0.0f ? ToleranceScale / matrixScale : ToleranceScale;
    PDFUtils::EmitPath(path, false, contentEntry.stream(), tolerance);
  }
  PDFUtils::PaintPath(path.getFillType(), contentEntry.stream());
  clearMaskOnGraphicState(contentEntry.stream());
}

}  // namespace tgfx