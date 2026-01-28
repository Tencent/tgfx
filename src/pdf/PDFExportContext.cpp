/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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
#include "core/AdvancedTypefaceInfo.h"
#include "core/DrawContext.h"
#include "core/GlyphRun.h"
#include "core/MCState.h"
#include "core/MeasureContext.h"
#include "core/PictureRecords.h"
#include "core/RunRecord.h"
#include "core/ScalerContext.h"
#include "core/filters/DropShadowImageFilter.h"
#include "core/filters/GaussianBlurImageFilter.h"
#include "core/filters/InnerShadowImageFilter.h"
#include "core/filters/ShaderMaskFilter.h"
#include "core/images/PictureImage.h"
#include "core/shaders/ColorShader.h"
#include "core/shaders/ImageShader.h"
#include "core/shaders/MatrixShader.h"
#include "core/utils/ColorHelper.h"
#include "core/utils/Log.h"
#include "core/utils/PlacementPtr.h"
#include "core/utils/ShapeUtils.h"
#include "core/utils/Types.h"
#include "pdf/PDFBitmap.h"
#include "pdf/PDFDocumentImpl.h"
#include "pdf/PDFFont.h"
#include "pdf/PDFFormXObject.h"
#include "pdf/PDFGraphicState.h"
#include "pdf/PDFResourceDictionary.h"
#include "pdf/PDFShader.h"
#include "pdf/PDFTypes.h"
#include "pdf/PDFUtils.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/Brush.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/ColorType.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/FontMetrics.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/ImageInfo.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/PathTypes.h"
#include "tgfx/core/Picture.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/SamplingOptions.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/Size.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/core/Surface.h"
#include "tgfx/core/TileMode.h"
#include "tgfx/core/Typeface.h"
#include "tgfx/core/UTF.h"
#include "tgfx/core/WriteStream.h"
#include "tgfx/layers/filters/DropShadowFilter.h"

namespace tgfx {

// A helper class to automatically finish a ContentEntry at the end of a
// drawing method and maintain the state needed between set up and finish.
class ScopedContentEntry {
 public:
  ScopedContentEntry(PDFExportContext* device, const MCState& state, const Matrix& matrix,
                     const Brush& brush, float textScale = 0)
      : drawContext(device), state(state) {
    blendMode = brush.blendMode;
    contentStream =
        drawContext->setUpContentEntry(state, matrix, brush, textScale, &destFormXObject);
  }

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
  Path path;
  const MCState& state;
};

PDFExportContext::PDFExportContext(ISize pageSize, PDFDocumentImpl* document,
                                   const Matrix& transform)
    : _pageSize(pageSize), document(document), _initialTransform(transform) {
  DEBUG_ASSERT(!_pageSize.isEmpty());
  content = MemoryWriteStream::Make();
}

PDFExportContext::~PDFExportContext() = default;

void PDFExportContext::reset() {
  content.reset();
}

void PDFExportContext::drawFill(const Brush& brush) {
  Path path;
  path.addRect(Rect::MakeSize(_pageSize));
  onDrawPath(MCState(), path, brush);
};

void PDFExportContext::drawRect(const Rect& rect, const MCState& state, const Brush& brush,
                                const Stroke* stroke) {
  Path path;
  path.addRect(rect);
  if (stroke) {
    stroke->applyToPath(&path);
  }
  onDrawPath(state, path, brush);
}

void PDFExportContext::drawRRect(const RRect& rRect, const MCState& state, const Brush& brush,
                                 const Stroke* stroke) {
  Path path;
  path.addRRect(rRect);
  if (stroke) {
    stroke->applyToPath(&path);
  }
  onDrawPath(state, path, brush);
}

void PDFExportContext::drawPath(const Path& path, const MCState& state, const Brush& brush) {
  this->onDrawPath(state, path, brush);
};

void PDFExportContext::drawShape(std::shared_ptr<Shape> shape, const MCState& state,
                                 const Brush& brush, const Stroke* stroke) {
  shape = Shape::ApplyStroke(std::move(shape), stroke);
  auto path = ShapeUtils::GetShapeRenderingPath(shape, state.matrix.getMaxScale());
  this->onDrawPath(state, path, brush);
}

void PDFExportContext::drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                                 const MCState& state, const Brush& brush) {
  auto rect = Rect::MakeWH(image->width(), image->height());
  onDrawImageRect(image, rect, sampling, state, brush);
}

void PDFExportContext::drawImageRect(std::shared_ptr<Image> image, const Rect& srcRect,
                                     const Rect& dstRect, const SamplingOptions& sampling,
                                     const MCState& state, const Brush& brush, SrcRectConstraint) {
  auto subsetImage = image->makeSubset(srcRect);
  if (subsetImage == nullptr) {
    return;
  }
  onDrawImageRect(image, dstRect, sampling, state, brush);
}
namespace {
enum class BlendFastPath {
  Normal,      // draw normally
  SrcOver,     //< draw as if in srcover mode
  SkipDrawing  //< draw nothing
};

bool just_solid_color(const Brush& brush) {
  return brush.isOpaque() && !brush.colorFilter && !brush.shader;
}

BlendFastPath check_fast_path(const Brush& brush, bool dstIsOpaque) {
  const auto blendMode = brush.blendMode;
  switch (blendMode) {
    case BlendMode::SrcOver:
      return BlendFastPath::SrcOver;
    case BlendMode::Src:
      if (just_solid_color(brush)) {
        return BlendFastPath::SrcOver;
      }
      return BlendFastPath::Normal;
    case BlendMode::Dst:
      return BlendFastPath::SkipDrawing;
    case BlendMode::DstOver:
      if (dstIsOpaque) {
        return BlendFastPath::SkipDrawing;
      }
      return BlendFastPath::Normal;
    case BlendMode::SrcIn:
      if (dstIsOpaque && just_solid_color(brush)) {
        return BlendFastPath::SrcOver;
      }
      return BlendFastPath::Normal;
    case BlendMode::DstIn:
      if (just_solid_color(brush)) {
        return BlendFastPath::SkipDrawing;
      }
      return BlendFastPath::Normal;
    default:
      return BlendFastPath::Normal;
  }
}

void remove_color_filter(Brush& brush) {
  if (auto filter = brush.colorFilter) {
    if (auto shader = brush.shader) {
      shader = shader->makeWithColorFilter(filter);
      brush.shader = shader;
    } else {
      //TODO (YGaurora): filter->filterColor() with color space
    }
    brush.colorFilter = nullptr;
  }
}

Brush clean_paint(const Brush& srcBrush) {
  Brush brush(srcBrush);
  if (brush.blendMode != BlendMode::SrcOver &&
      check_fast_path(brush, false) == BlendFastPath::SrcOver) {
    brush.blendMode = BlendMode::SrcOver;
  }
  if (brush.colorFilter) {
    // We assume here that PDFs all draw in sRGB.
    remove_color_filter(brush);
  }
  return brush;
}

int add_resource(std::unordered_set<PDFIndirectReference>& resources, PDFIndirectReference ref) {
  resources.insert(ref);
  return ref.value;
}
}  // namespace

namespace {
class GlyphPositioner {
 public:
  GlyphPositioner(std::shared_ptr<MemoryWriteStream> content, float textSkewX, Point origin)
      : content(std::move(content)), currentMatrixOrigin(origin), textSkewX(textSkewX) {
  }
  ~GlyphPositioner() {
    this->flush();
  }
  void flush() {
    if (inText) {
      content->writeText("> Tj\n");
      inText = false;
    }
  }
  void setFont(PDFFont* pdfFont) {
    this->flush();
    _pdfFont = pdfFont;
    // Reader 2020.013.20064 incorrectly advances some Type3 fonts https://crbug.com/1226960
    bool convertedToType3 = _pdfFont->getType() == AdvancedTypefaceInfo::FontType::Other;
    bool thousandEM = _pdfFont->strike().strikeSpec.unitsPerEM == 1000;
    viewersAgreeOnAdvancesInFont = thousandEM || !convertedToType3;
  }

  void writeGlyph(uint16_t glyph, float advanceWidth, Point xy) {
    if (!initialized) {
      // Flip the text about the x-axis to account for origin swap and include
      // the passed parameters.
      content->writeText("1 0 ");
      PDFUtils::AppendFloat(-textSkewX, content);
      content->writeText(" -1 ");
      PDFUtils::AppendFloat(currentMatrixOrigin.x, content);
      content->writeText(" ");
      PDFUtils::AppendFloat(currentMatrixOrigin.y, content);
      content->writeText(" Tm\n");
      currentMatrixOrigin.set(0.0f, 0.0f);
      initialized = true;
    }
    Point position = xy - currentMatrixOrigin;
    if (!viewersAgreeOnXAdvance || position != Point{XAdvance, 0}) {
      this->flush();
      PDFUtils::AppendFloat(position.x - (position.y * textSkewX), content);
      content->writeText(" ");
      PDFUtils::AppendFloat(-position.y, content);
      content->writeText(" Td ");
      currentMatrixOrigin = xy;
      XAdvance = 0;
      viewersAgreeOnXAdvance = true;
    }
    XAdvance += advanceWidth;
    if (!viewersAgreeOnAdvancesInFont) {
      viewersAgreeOnXAdvance = false;
    }
    if (!inText) {
      content->writeText("<");
      inText = true;
    }
    if (_pdfFont->multiByteGlyphs()) {
      PDFUtils::WriteUInt16BE(content, glyph);
    } else {
      PDFUtils::WriteUInt8(content, static_cast<uint8_t>(glyph));
    }
  }

 private:
  std::shared_ptr<MemoryWriteStream> content;
  PDFFont* _pdfFont = nullptr;
  Point currentMatrixOrigin;
  float XAdvance = 0.0f;
  bool viewersAgreeOnAdvancesInFont = true;
  bool viewersAgreeOnXAdvance = true;
  float textSkewX;
  bool inText = false;
  bool initialized = false;
};

Unichar MapGlyph(const std::vector<Unichar>& glyphToUnicode, GlyphID glyph) {
  return glyph < glyphToUnicode.size() ? glyphToUnicode[glyph] : -1;
}

bool NeedsNewFont(PDFFont* font, GlyphID glyphID, AdvancedTypefaceInfo::FontType initialFontType) {
  if (!font || !font->hasGlyph(glyphID)) {
    return true;
  }
  if (initialFontType == AdvancedTypefaceInfo::FontType::Other) {
    return false;
  }

  auto scaleContext = PDFFont::GetScalerContext(font->strike().strikeSpec.typeface,
                                                font->strike().strikeSpec.textSize);
  Path glyphPath;
  bool hasUnmodifiedPath = scaleContext->generatePath(glyphID, false, false, &glyphPath);
  bool convertedToType3 = font->getType() == AdvancedTypefaceInfo::FontType::Other;
  return convertedToType3 == hasUnmodifiedPath;
}
}  // namespace

void PDFExportContext::drawTextBlob(std::shared_ptr<TextBlob> textBlob, const MCState& state,
                                    const Brush& brush, const Stroke* stroke) {
  for (auto glyphRun : *textBlob) {
    onDrawGlyphRun(glyphRun, state, brush, stroke);
  }
}

void PDFExportContext::onDrawGlyphRun(const GlyphRun& glyphRun, const MCState& state,
                                      const Brush& brush, const Stroke* stroke) {

  auto& font = glyphRun.font;
  auto typeface = font.getTypeface();
  // RSXform/Matrix positioning requires path export since PDF text operators cannot represent
  // per-glyph rotation/scale.
  if (!typeface->isCustom()) {
    if (font.hasColor()) {
      exportGlyphRunAsImage(glyphRun, state, brush);
    } else if (HasComplexTransform(glyphRun) || brush.maskFilter || stroke) {
      exportGlyphRunAsPath(glyphRun, state, brush, stroke);
    } else {
      exportGlyphRunAsText(glyphRun, state, brush);
    }
  } else {
    if (font.hasColor()) {
      exportGlyphRunAsImage(glyphRun, state, brush);
    } else {
      exportGlyphRunAsPath(glyphRun, state, brush, stroke);
    }
  }
}

void PDFExportContext::exportGlyphRunAsText(const GlyphRun& glyphRun, const MCState& state,
                                            const Brush& brush) {
  if (glyphRun.glyphCount == 0) {
    return;
  }

  const auto& glyphRunFont = glyphRun.font;
  auto pdfStrike = PDFStrike::Make(document, glyphRunFont);
  if (!pdfStrike) {
    return;
  }

  auto typeface = pdfStrike->strikeSpec.typeface;
  auto textSize = pdfStrike->strikeSpec.textSize;

  const auto advancedInfo = PDFFont::GetAdvancedInfo(typeface, textSize, document);
  if (!advancedInfo) {
    return;
  }

  const auto& glyphToUnicode = PDFFont::GetUnicodeMap(*typeface, document);

  AdvancedTypefaceInfo::FontType initialFontType = PDFFont::FontType(*pdfStrike, *advancedInfo);

  // The size, skewX, and scaleX are applied here.
  float advanceScale = textSize * 1.f / pdfStrike->strikeSpec.unitsPerEM;

  // textScaleX and textScaleY are used to get a conservative bounding box for glyphs.
  float textScaleY = textSize / pdfStrike->strikeSpec.unitsPerEM;
  float textScaleX = advanceScale;

  const auto clipStackBounds =
      state.clip.isEmpty() ? Rect::MakeSize(_pageSize) : state.clip.getBounds();

  // Clear everything from the runPaint that will be applied by the strike.
  Brush brushPaint(brush);
  brushPaint.maskFilter = nullptr;
  auto paint = clean_paint(brushPaint);
  ScopedContentEntry content(this, state, Matrix::I(), paint);
  if (!content) {
    return;
  }
  auto out = content.stream();

  out->writeText("BT\n");
  {
    // Destinations are in absolute coordinates.
    // The glyphs bounds go through the localToDevice separately for clipping.
    Matrix pageXform = state.matrix;
    pageXform.postConcat(document->currentPageTransform());

    const auto numGlyphs = typeface->glyphsCount();
    auto offsetMatrix = ComputeGlyphMatrix(glyphRun, 0);
    auto offset = Point::Make(offsetMatrix.getTranslateX(), offsetMatrix.getTranslateY());
    GlyphPositioner glyphPositioner(out, glyphRunFont.getMetrics().leading, offset);
    PDFFont* font = nullptr;

    for (size_t index = 0; index < glyphRun.glyphCount; ++index) {
      auto glyphID = glyphRun.glyphs[index];

      glyphPositioner.flush();
      out->writeText("/Span<</ActualText ");
      auto unichar = MapGlyph(glyphToUnicode, glyphID);
      auto utf8Text = UTF::ToUTF8(unichar);
      PDFWriteTextString(out, utf8Text);
      // begin marked-content sequence with an associated property list.
      out->writeText(" >> BDC\n");
      if (numGlyphs <= glyphID) {
        continue;
      }
      auto xyMatrix = ComputeGlyphMatrix(glyphRun, index);
      auto xy = Point::Make(xyMatrix.getTranslateX(), xyMatrix.getTranslateY());
      // Do a glyph-by-glyph bounds-reject if positions are absolute.
      auto glyphBounds = glyphRunFont.getBounds(glyphID);
      glyphBounds = Matrix::MakeScale(textScaleX, textScaleY).mapRect(glyphBounds);
      glyphBounds.offset(xy + offset);
      state.matrix.mapRect(&glyphBounds);

      if (glyphBounds.isEmpty()) {
        if (!clipStackBounds.contains(glyphBounds.x(), glyphBounds.y())) {
          continue;
        }
      } else {
        if (!Rect::Intersects(clipStackBounds, glyphBounds)) {
          continue;
        }
      }
      if (NeedsNewFont(font, glyphID, initialFontType)) {
        // Not yet specified font or need to switch font.
        font = pdfStrike->getFontResource(glyphID);
        DEBUG_ASSERT(font);
        glyphPositioner.setFont(font);
        PDFWriteResourceName(out, PDFResourceType::Font,
                             add_resource(fontResources, font->indirectReference()));
        out->writeText(" ");
        PDFUtils::AppendFloat(textSize, out);
        out->writeText(" Tf\n");
      }
      font->noteGlyphUsage(glyphID);
      GlyphID encodedGlyph = font->glyphToPDFFontEncoding(glyphID);
      float advance = advanceScale * glyphRunFont.getAdvance(glyphID);
      glyphPositioner.writeGlyph(encodedGlyph, advance, xy);
    }
    // if (actualText)
    {
      glyphPositioner.flush();
      out->writeText("EMC\n");
    }
  }
  out->writeText("ET\n");
}

void PDFExportContext::exportGlyphRunAsPath(const GlyphRun& glyphRun, const MCState& state,
                                            const Brush& brush, const Stroke* stroke) {
  const auto& glyphFont = glyphRun.font;
  Path path;

  for (size_t i = 0; i < glyphRun.glyphCount; ++i) {
    auto glyphID = glyphRun.glyphs[i];
    auto glyphMatrix = ComputeGlyphMatrix(glyphRun, i);
    Path glyphPath;
    if (!glyphFont.getPath(glyphID, &glyphPath)) {
      continue;
    }
    glyphPath.transform(glyphMatrix);
    path.addPath(glyphPath);
  }

  if (path.isEmpty()) {
    return;
  }
  auto shape = Shape::MakeFrom(path);
  drawShape(shape, state, brush, stroke);

  //TODO (YGaurora): maybe hasPerspective()
  Brush transparentBrush = brush;
  transparentBrush.color = Color::Transparent();
  exportGlyphRunAsText(glyphRun, state, transparentBrush);
}

void PDFExportContext::exportGlyphRunAsImage(const GlyphRun& glyphRun, const MCState& state,
                                             const Brush& brush) {
  const auto& glyphFont = glyphRun.font;
  for (size_t i = 0; i < glyphRun.glyphCount; ++i) {
    auto glyphID = glyphRun.glyphs[i];
    auto glyphMatrix = ComputeGlyphMatrix(glyphRun, i);
    auto tempState = state;
    Matrix matrix;
    auto glyphImageCodec = glyphFont.getImage(glyphID, nullptr, &matrix);
    if (glyphImageCodec == nullptr) {
      continue;
    }
    tempState.matrix.preConcat(matrix);
    tempState.matrix.postConcat(glyphMatrix);

    auto glyphImage = Image::MakeFrom(glyphImageCodec);
    auto rect = Rect::MakeWH(glyphImage->width(), glyphImage->height());
    drawImageRect(std::move(glyphImage), rect, rect, {}, tempState, brush, SrcRectConstraint::Fast);
  }

  //TODO (YGaurora): maybe hasPerspective()
  Brush transparentBrush = brush;
  transparentBrush.color = Color::Transparent();
  exportGlyphRunAsText(glyphRun, state, transparentBrush);
}

void PDFExportContext::drawPicture(std::shared_ptr<Picture> picture, const MCState& state) {
  picture->playback(this, state);
}

void PDFExportContext::drawDropShadowBeforeLayer(const std::shared_ptr<Picture>& picture,
                                                 const DropShadowImageFilter* dropShadowFilter,
                                                 const MCState& state, const Brush& brush) {
  DEBUG_ASSERT(Types::Get(dropShadowFilter->blurFilter.get()) == Types::ImageFilterType::Blur);
  const auto blurFilter =
      static_cast<const GaussianBlurImageFilter*>(dropShadowFilter->blurFilter.get());
  auto copyFilter = ImageFilter::DropShadowOnly(0.f, 0.f, blurFilter->blurrinessX,
                                                blurFilter->blurrinessY, dropShadowFilter->color);

  auto pictureBounds = picture->getBounds();
  auto blurBounds = copyFilter->filterBounds(pictureBounds);
  auto offset = Point::Make(pictureBounds.x() - blurBounds.x(), pictureBounds.y() - blurBounds.y());

  auto surface = Surface::Make(document->context(), static_cast<int>(blurBounds.width()),
                               static_cast<int>(blurBounds.height()), false, 1, false, 0,
                               document->dstColorSpace());
  DEBUG_ASSERT(surface);
  auto canvas = surface->getCanvas();

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
    drawImage(std::move(image), SamplingOptions(), imageState, brush);
  }
}

void PDFExportContext::drawInnerShadowAfterLayer(const PictureRecord* record,
                                                 const InnerShadowImageFilter* innerShadowFilter,
                                                 const MCState& state) {
  MeasureContext measureContext;
  PlaybackContext playbackContext = {};
  record->playback(&measureContext, &playbackContext);
  auto pictureBounds = measureContext.getBounds();
  if (pictureBounds.isEmpty()) {
    return;
  }

  auto surface = Surface::Make(document->context(), static_cast<int>(pictureBounds.width()),
                               static_cast<int>(pictureBounds.height()), false, 1, false, 0,
                               document->dstColorSpace());
  DEBUG_ASSERT(surface);
  auto canvas = surface->getCanvas();
  canvas->translate(-pictureBounds.x(), -pictureBounds.y());

  DEBUG_ASSERT(Types::Get(innerShadowFilter->blurFilter.get()) == Types::ImageFilterType::Blur);
  const auto blurFilter =
      static_cast<const GaussianBlurImageFilter*>(innerShadowFilter->blurFilter.get());
  auto copyFilter = ImageFilter::InnerShadowOnly(innerShadowFilter->dx, innerShadowFilter->dy,
                                                 blurFilter->blurrinessX, blurFilter->blurrinessY,
                                                 innerShadowFilter->color);

  Paint picturePaint = {};
  picturePaint.setImageFilter(copyFilter);

  canvas->saveLayer(&picturePaint);
  {
    auto surfaceContext = canvas->drawContext;
    auto matrix = Matrix::MakeTrans(-pictureBounds.x(), -pictureBounds.y());
    PlaybackContext tempPlaybackContext = {};
    tempPlaybackContext.setMatrix(matrix);
    tempPlaybackContext.setClip(state.clip);
    record->playback(surfaceContext, &playbackContext);
  }
  canvas->restore();

  {
    auto image = surface->makeImageSnapshot();
    auto imageShader = Shader::MakeImageShader(image);
    imageShader =
        imageShader->makeWithMatrix(Matrix::MakeTrans(pictureBounds.x(), pictureBounds.y()));
    PlaybackContext tempPlaybackContext(state);
    Brush tempBrush;
    tempBrush.shader = imageShader;
    tempPlaybackContext.setBrush(tempBrush);
    record->playback(this, &tempPlaybackContext);
  }
}

void PDFExportContext::drawBlurLayer(const std::shared_ptr<Picture>& picture,
                                     const std::shared_ptr<ImageFilter>& imageFilter,
                                     const MCState& state, const Brush& brush) {
  auto pictureBounds = picture->getBounds();
  auto blurBounds = imageFilter->filterBounds(pictureBounds);
  blurBounds = blurBounds.makeOutset(100, 100);
  Point offset = {pictureBounds.x() - blurBounds.x(), pictureBounds.y() - blurBounds.y()};

  auto surface = Surface::Make(document->context(), static_cast<int>(blurBounds.width()),
                               static_cast<int>(blurBounds.height()), false, 1, false, 0,
                               document->dstColorSpace());
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
    drawImage(std::move(image), SamplingOptions(), imageState, brush);
  }
}

void PDFExportContext::drawLayer(std::shared_ptr<Picture> picture,
                                 std::shared_ptr<ImageFilter> imageFilter, const MCState& state,
                                 const Brush& brush) {

  if (imageFilter) {
    if (Types::Get(imageFilter.get()) == Types::ImageFilterType::DropShadow) {
      const auto dropShadowFilter = static_cast<const DropShadowImageFilter*>(imageFilter.get());
      drawDropShadowBeforeLayer(picture, dropShadowFilter, state, brush);
      if (!dropShadowFilter->shadowOnly) {
        picture->playback(this, state);
      }
      return;
    }
    if (Types::Get(imageFilter.get()) == Types::ImageFilterType::InnerShadow) {
      const auto innerShadowFilter = static_cast<const InnerShadowImageFilter*>(imageFilter.get());
      PlaybackContext playbackContext = {};
      for (const auto& record : picture->records) {
        record->playback(this, &playbackContext);
        drawInnerShadowAfterLayer(record.get(), innerShadowFilter, state);
      }
      return;
    }
    if (Types::Get(imageFilter.get()) == Types::ImageFilterType::Blur) {
      drawBlurLayer(picture, imageFilter, state, brush);
      return;
    }
  }
  picture->playback(this, state);
}

std::shared_ptr<Data> PDFExportContext::getContent() {
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

void PDFExportContext::onDrawPath(const MCState& state, const Path& path, const Brush& brush) {
  if (brush.maskFilter) {
    this->drawPathWithFilter(state, path, Matrix::I(), brush);
    return;
  }

  Matrix matrix = Matrix::I();
  ScopedContentEntry scopedContent(this, state, matrix, brush);
  if (!scopedContent) {
    return;
  }

  PDFUtils::EmitPath(path, false, content);
  PDFUtils::PaintPath(path.getFillType(), content);
}

void PDFExportContext::onDrawImageRect(std::shared_ptr<Image> image, const Rect& rect,
                                       const SamplingOptions& sampling, const MCState& state,
                                       const Brush& brush) {
  if (!image) {
    return;
  }

  // First, figure out the src->dst transform and subset the image if needed.
  auto bound = Rect::MakeWH(image->width(), image->height());
  float scaleX = rect.width() / bound.width();
  float scaleY = rect.height() / bound.height();
  float transX = rect.left - (bound.left * scaleX);
  float transY = rect.top - (bound.top * scaleY);
  auto transform = Matrix::I();
  transform.postScale(scaleX, scaleY);
  transform.postTranslate(transX, transY);

  // Alpha-only images need to get their color from the shader, before applying the colorfilter.
  auto modifiedBrush = brush;
  if (image->isAlphaOnly() && modifiedBrush.colorFilter) {
    // must blend alpha image and shader before applying colorfilter.
    auto surface = Surface::Make(document->context(), image->width(), image->height(), false, 1,
                                 false, 0, document->dstColorSpace());
    Canvas* canvas = surface->getCanvas();
    Paint tmpPaint;
    // In the case of alpha images with shaders, the shader's coordinate system is the image's
    // coordiantes.
    tmpPaint.setShader(modifiedBrush.shader);
    tmpPaint.setColor(modifiedBrush.color);
    canvas->clear();
    canvas->drawImage(image, &tmpPaint);
    if (modifiedBrush.shader != nullptr) {
      modifiedBrush.shader = nullptr;
    }
    image = surface->makeImageSnapshot();
    DEBUG_ASSERT(!image->isAlphaOnly());
  }

  if (image->isAlphaOnly()) {
    // The ColorFilter applies to the paint color/shader, not the alpha layer.
    DEBUG_ASSERT(modifiedBrush.colorFilter == nullptr);

    // PDF doesn't seem to allow masking vector graphics with an Image XObject. Must mask with a
    // Form XObject.
    auto maskContext = PDFExportContext(ISize::Make(image->width(), image->height()), document);
    {
      auto canvas = PDFDocumentImpl::MakeCanvas(&maskContext);
      // This clip prevents the mask image shader from covering entire device if unnecessary.
      canvas->clipRect(state.clip.getBounds());
      if (modifiedBrush.maskFilter) {
        Paint tmpPaint;
        auto imageShader =
            Shader::MakeImageShader(image, TileMode::Clamp, TileMode::Clamp, SamplingOptions());
        imageShader = imageShader->makeWithMatrix(transform);
        tmpPaint.setShader(imageShader);
        tmpPaint.setMaskFilter(modifiedBrush.maskFilter);
        canvas->drawRect(rect, tmpPaint);
      } else {
        canvas->concat(transform);
        canvas->drawImage(image, sampling);
      }
    }
    // SkIRect maskDeviceBounds = maskDevice->cs().bounds(maskDevice->bounds()).roundOut();
    auto maskDeviceBounds = Rect::MakeSize(maskContext.pageSize());
    ScopedContentEntry content(this, state, Matrix::I(), modifiedBrush);
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
  if (modifiedBrush.maskFilter) {
    auto imageShader =
        Shader::MakeImageShader(image, TileMode::Clamp, TileMode::Clamp, SamplingOptions());
    imageShader = imageShader->makeWithMatrix(transform);
    modifiedBrush.shader = imageShader;

    Path path;
    path.addRect(rect);
    this->onDrawPath(state, path, modifiedBrush);
    return;
  }

  auto matrix = transform;
  Matrix scaled;
  // Adjust for origin flip.
  scaled.setScale(1.f, -1.f);
  scaled.postTranslate(0, 1.f);
  // Scale the image up from 1x1 to WxH.
  auto subset = Rect::MakeWH(image->width(), image->height());
  scaled.postScale(subset.width(), subset.height());
  scaled.postConcat(matrix);
  ScopedContentEntry content(this, state, scaled, modifiedBrush);
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

  if (auto colorFilter = modifiedBrush.colorFilter) {
    auto imageFilter = ImageFilter::ColorFilter(colorFilter);
    image = image->makeWithFilter(imageFilter);
    if (!image) {
      return;
    }
  }

  PDFIndirectReference pdfImage =
      PDFBitmap::Serialize(image, document, document->metadata().encodingQuality);
  this->drawFormXObject(pdfImage, content.stream(), &shape);
}

namespace {
std::vector<PDFIndirectReference> Sort(const std::unordered_set<PDFIndirectReference>& src) {
  std::vector<PDFIndirectReference> dst;
  dst.reserve(src.size());
  for (auto ref : src) {
    dst.push_back(ref);
  }
  std::sort(dst.begin(), dst.end(),
            [](PDFIndirectReference a, PDFIndirectReference b) { return a.value < b.value; });
  return dst;
}
}  // namespace

std::unique_ptr<PDFDictionary> PDFExportContext::makeResourceDict() {
  return MakePDFResourceDictionary(Sort(graphicStateResources), Sort(shaderResources),
                                   Sort(xObjectResources), Sort(fontResources));
}

std::unique_ptr<PDFDictionary> PDFExportContext::makeResourceDictionary() {
  return MakePDFResourceDictionary(Sort(graphicStateResources), Sort(shaderResources),
                                   Sort(xObjectResources), Sort(fontResources));
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

bool TreatAsRegularPDFBlendMode(BlendMode blendMode) {
  return PDFUtils::BlendModeName(blendMode) != nullptr;
}

void PopulateGraphicStateEntryFromPaint(
    PDFDocumentImpl* document, const Matrix& matrix, const MCState& state, Rect deviceBounds,
    const Brush& brush, const Matrix& initialTransform, float textScale,
    const std::shared_ptr<ColorSpace>& colorSpace, PDFGraphicStackState::Entry* entry,
    std::unordered_set<PDFIndirectReference>* shaderResources,
    std::unordered_set<PDFIndirectReference>* graphicStateResources) {

  entry->matrix = state.matrix * matrix;
  auto color = brush.color;
  color.alpha = 1;
  entry->color = color;
  entry->shaderIndex = -1;

  // PDF treats a shader as a color, so we only set one or the other.
  if (auto shader = brush.shader) {
    // note: we always present the alpha as 1 for the shader, knowing that it will be accounted for
    // when we create our newGraphicsState (below)
    if (Types::Get(shader.get()) == Types::ShaderType::Color) {
      const auto colorShader = static_cast<const ColorShader*>(shader.get());
      if (colorShader->asColor(&color)) {
        color.alpha = 1;
        entry->color = color;
      }
    } else {
      // PDF positions patterns relative to the initial transform, so we need to apply the current
      // transform to the shader parameters.
      auto transform = entry->matrix;
      transform.postConcat(initialTransform);

      // PDF doesn't support kClamp_TileMode, so we simulate it by making a pattern the size of the
      // current clip.
      Rect clipStackBounds = deviceBounds;

      // We need to apply the initial transform to bounds in order to get bounds in a consistent
      // coordinate system.
      initialTransform.mapRect(&clipStackBounds);
      clipStackBounds.roundOut();

      PDFIndirectReference pdfShader =
          PDFShader::Make(document, shader, transform, clipStackBounds, color);
      if (pdfShader) {
        // pdfShader has been canonicalized so we can directly compare pointers.
        entry->shaderIndex = add_resource(*shaderResources, pdfShader);
      }
    }
  }

  auto newGraphicState = PDFGraphicState::GetGraphicStateForPaint(document, brush);
  entry->graphicStateIndex = add_resource(*graphicStateResources, newGraphicState);
  entry->textScaleX = textScale;
  entry->color = ConvertColorSpace(entry->color, colorSpace);
}

}  // namespace

std::shared_ptr<MemoryWriteStream> PDFExportContext::setUpContentEntry(
    const MCState& state, const Matrix& matrix, const Brush& brush, float scale,
    PDFIndirectReference* destination) {
  DEBUG_ASSERT(!*destination);
  BlendMode blendMode = brush.blendMode;

  if (blendMode == BlendMode::Dst) {
    return nullptr;
  }

  if (!TreatAsRegularPDFBlendMode(blendMode) && blendMode != BlendMode::DstOver) {
    if (!isContentEmpty()) {
      *destination = this->makeFormXObjectFromDevice();
      DEBUG_ASSERT(isContentEmpty());
    } else if (blendMode != BlendMode::Src && blendMode != BlendMode::SrcOut) {
      return nullptr;
    }
  }

  if (TreatAsRegularPDFBlendMode(blendMode)) {
    if (!fActiveStackState.contentStream) {
      if (content->bytesWritten() != 0) {
        content->writeText("Q\nq\n");
        needsExtraSave = true;
      }
      fActiveStackState = PDFGraphicStackState(content);
    } else {
      DEBUG_ASSERT(fActiveStackState.contentStream == content);
    }
  } else {
    fActiveStackState.drainStack();
    fActiveStackState = PDFGraphicStackState(contentBuffer);
  }

  DEBUG_ASSERT(fActiveStackState.contentStream);
  PDFGraphicStackState::Entry entry;
  PopulateGraphicStateEntryFromPaint(document, matrix, state, Rect::MakeSize(_pageSize), brush,
                                     _initialTransform, scale, document->dstColorSpace(), &entry,
                                     &shaderResources, &graphicStateResources);
  fActiveStackState.updateClip(state);
  fActiveStackState.updateMatrix(entry.matrix);
  fActiveStackState.updateDrawingState(entry, document->colorSpaceRef());

  return fActiveStackState.contentStream;
}

void PDFExportContext::finishContentEntry(const MCState& state, BlendMode blendMode,
                                          PDFIndirectReference destination, Path* path) {
  DEBUG_ASSERT(blendMode != BlendMode::Dst);
  if (TreatAsRegularPDFBlendMode(blendMode)) {
    DEBUG_ASSERT(!destination);
    return;
  }

  DEBUG_ASSERT(fActiveStackState.contentStream);

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
  // Changing the current content into a form-xobject will destroy the clip objects which is fine
  // since the xobject will already be clipped. However if source has shape, we need to clip it too,
  // so a copy of the clip is saved.

  Brush stockBrush;
  PDFIndirectReference srcFormXObject;
  if (this->isContentEmpty()) {
    // If nothing was drawn and there's no shape, then the draw was a no-op, but dst needs to be
    // restored for that to be true. If there is shape, then an empty source with Src, SrcIn,
    // SrcOut, DstIn, DstAtop or Modulate reduces to Clear and DstOut or SrcAtop reduces to Dst.
    if (path == nullptr || blendMode == BlendMode::DstOut || blendMode == BlendMode::SrcATop) {
      ScopedContentEntry contentEntry(this, MCState(), Matrix::I(), stockBrush);
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
      Brush filledBrush;
      filledBrush.color = Color::Black();
      MCState empty;
      PDFExportContext shapeContext(_pageSize, document, _initialTransform);
      shapeContext.onDrawPath(state, *path, filledBrush);
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
    ScopedContentEntry content(this, MCState(), Matrix::I(), stockBrush);
    if (content) {
      this->drawFormXObject(srcFormXObject, content.stream(), nullptr);
    }
    if (blendMode == BlendMode::Src) {
      return;
    }
  } else if (blendMode == BlendMode::SrcATop) {
    ScopedContentEntry content(this, MCState(), Matrix::I(), stockBrush);
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
                                       const std::shared_ptr<MemoryWriteStream>& stream,
                                       Path* shape) {
  auto point = Point::Zero();
  if (shape) {
    // Destinations are in absolute coordinates.
    auto pageXform = document->currentPageTransform();
    // The shape already has localToDevice applied.
    auto shapeBounds = shape->getBounds();
    pageXform.mapRect(&shapeBounds);
    point = Point{shapeBounds.left, shapeBounds.bottom};
  }

  DEBUG_ASSERT(xObject);
  PDFWriteResourceName(stream, PDFResourceType::XObject, add_resource(xObjectResources, xObject));
  content->writeText(" Do\n");
}

void PDFExportContext::clearMaskOnGraphicState(const std::shared_ptr<MemoryWriteStream>& stream) {
  auto& noSMaskGS = document->noSmaskGraphicState;
  if (!noSMaskGS) {
    auto tmp = PDFDictionary::Make("ExtGState");
    tmp->insertName("SMask", "None");
    noSMaskGS = document->emit(*tmp);
  }
  this->setGraphicState(noSMaskGS, stream);
}

void PDFExportContext::setGraphicState(PDFIndirectReference graphicState,
                                       const std::shared_ptr<MemoryWriteStream>& stream) {
  PDFUtils::ApplyGraphicState(add_resource(graphicStateResources, graphicState), stream);
}

void PDFExportContext::drawFormXObjectWithMask(PDFIndirectReference xObject,
                                               PDFIndirectReference sMask, BlendMode mode,
                                               bool invertClip) {
  DEBUG_ASSERT(sMask);
  Brush brush;
  brush.blendMode = mode;
  ScopedContentEntry content(this, MCState(), Matrix::I(), brush);
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
    if (Types::Get(shader.get()) == Types::ShaderType::Matrix) {
      const auto matrixShader = static_cast<const MatrixShader*>(shader.get());
      matrix.postConcat(matrixShader->matrix);
      shader = matrixShader->source;
    } else if (Types::Get(shader.get()) == Types::ShaderType::Image) {
      const auto imageShader = static_cast<const ImageShader*>(shader.get());
      auto image = imageShader->image;
      if (Types::Get(image.get()) == Types::ImageType::Picture) {
        const auto pictureImage = static_cast<const PictureImage*>(imageShader->image.get());
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
                                          const Matrix& matrix, const Brush& originPaint) {
  DEBUG_ASSERT(originPaint.maskFilter);

  Path path(originPath);
  path.transform(matrix);
  auto maskBound = path.getBounds();

  Brush paint(originPaint);

  if (Types::Get(originPaint.maskFilter.get()) != Types::MaskFilterType::Shader) {
    return;
  }
  const auto shaderMaskFilter = static_cast<const ShaderMaskFilter*>(originPaint.maskFilter.get());
  auto [picture, pictureMatrix] = MaskFilterToPicture(shaderMaskFilter);

  auto maskContext = makeCongruentDevice();
  if (!picture) {  //mask as image
    auto surface = Surface::Make(document->context(), static_cast<int>(maskBound.width()),
                                 static_cast<int>(maskBound.height()), false, 1, false, 0,
                                 document->dstColorSpace());
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
    grayscaleInfo = ImageInfo::Make(surface->width(), surface->height(), ColorType::Gray_8,
                                    AlphaType::Premultiplied, 0, document->dstColorSpace());
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
  ScopedContentEntry contentEntry(this, state, Matrix::I(), paint);
  if (!contentEntry) {
    return;
  }

  setGraphicState(
      PDFGraphicState::GetSMaskGraphicState(
          maskContext->makeFormXObjectFromDevice(maskBound, true), false,
          picture ? PDFGraphicState::SMaskMode::Alpha : PDFGraphicState::SMaskMode::Luminosity,
          document),
      contentEntry.stream());

  PDFUtils::EmitPath(path, false, contentEntry.stream());

  PDFUtils::PaintPath(path.getFillType(), contentEntry.stream());
  clearMaskOnGraphicState(contentEntry.stream());
}

}  // namespace tgfx
