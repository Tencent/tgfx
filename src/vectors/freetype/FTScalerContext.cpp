/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "FTScalerContext.h"
#include <cmath>
#include "ft2build.h"
#include FT_BITMAP_H
#include FT_OUTLINE_H
#include FT_SIZES_H
#include FT_TRUETYPE_TABLES_H
#include "FTUtil.h"
#include "skcms.h"
#include "tgfx/core/Pixmap.h"
#include "utils/Log.h"
#include "utils/MathExtra.h"

namespace tgfx {
//  See http://freetype.sourceforge.net/freetype2/docs/reference/ft2-bitmap_handling.html#FT_Bitmap_Embolden
//  This value was chosen by eyeballing the result in Firefox and trying to match it.
static constexpr FT_Pos BITMAP_EMBOLDEN_STRENGTH = 1 << 6;
static constexpr int OUTLINE_EMBOLDEN_DIVISOR = 24;
static constexpr float ITALIC_SKEW = -0.20f;

static float FTFixedToFloat(FT_Fixed x) {
  return static_cast<float>(x) * 1.52587890625e-5f;
}

static FT_Fixed FloatToFTFixed(float x) {
  static constexpr float MaxS32FitsInFloat = 2147483520.f;
  static constexpr float MinS32FitsInFloat = -MaxS32FitsInFloat;
  x = x < MaxS32FitsInFloat ? x : MaxS32FitsInFloat;
  x = x > MinS32FitsInFloat ? x : MinS32FitsInFloat;
  return static_cast<FT_Fixed>(x * (1 << 16));
}

std::unique_ptr<FTScalerContext> FTScalerContext::Make(std::shared_ptr<FTTypeface> typeface,
                                                       float size) {
  if (typeface == nullptr || typeface->face == nullptr) {
    return nullptr;
  }
  auto scalerContext =
      std::unique_ptr<FTScalerContext>(new FTScalerContext(std::move(typeface), size));
  if (!scalerContext->valid()) {
    return nullptr;
  }
  return scalerContext;
}

static void ApplyEmbolden(FT_Face face, FT_GlyphSlot glyph, GlyphID glyphId, FT_Int32 glyphFlags) {
  switch (glyph->format) {
    case FT_GLYPH_FORMAT_OUTLINE:
      FT_Pos strength;
      strength =
          FT_MulFix(face->units_per_EM, face->size->metrics.y_scale) / OUTLINE_EMBOLDEN_DIVISOR;
      FT_Outline_Embolden(&glyph->outline, strength);
      break;
    case FT_GLYPH_FORMAT_BITMAP:
      if (!face->glyph->bitmap.buffer) {
        FT_Load_Glyph(face, glyphId, glyphFlags);
      }
      FT_GlyphSlot_Own_Bitmap(glyph);
      FT_Bitmap_Embolden(glyph->library, &glyph->bitmap, BITMAP_EMBOLDEN_STRENGTH, 0);
      break;
    default:
      LOGE("unknown glyph format");
  }
}

/**
 * Returns the bitmap strike equal to or just larger than the requested size.
 */
static FT_Int ChooseBitmapStrike(FT_Face face, FT_F26Dot6 scaleY) {
  if (face == nullptr) {
    return -1;
  }

  FT_Pos requestedPPEM = scaleY;  // FT_Bitmap_Size::y_ppem is in 26.6 format.
  FT_Int chosenStrikeIndex = -1;
  FT_Pos chosenPPEM = 0;
  for (FT_Int strikeIndex = 0; strikeIndex < face->num_fixed_sizes; ++strikeIndex) {
    FT_Pos strikePPEM = face->available_sizes[strikeIndex].y_ppem;
    if (strikePPEM == requestedPPEM) {
      // exact match - our search stops here
      return strikeIndex;
    } else if (chosenPPEM < requestedPPEM) {
      // attempt to increase chosenPPEM
      if (chosenPPEM < strikePPEM) {
        chosenPPEM = strikePPEM;
        chosenStrikeIndex = strikeIndex;
      }
    } else {
      // attempt to decrease chosenPPEM, but not below requestedPPEM
      if (requestedPPEM < strikePPEM && strikePPEM < chosenPPEM) {
        chosenPPEM = strikePPEM;
        chosenStrikeIndex = strikeIndex;
      }
    }
  }
  return chosenStrikeIndex;
}

FTScalerContext::FTScalerContext(std::shared_ptr<FTTypeface> ftTypeface, float size)
    : typeface(std::move(ftTypeface)), textSize(size) {
  std::lock_guard<std::mutex> autoLock(typeface->locker);
  loadGlyphFlags |= FT_LOAD_NO_BITMAP;
  // Always using FT_LOAD_IGNORE_GLOBAL_ADVANCE_WIDTH to get correct
  // advances, as fontconfig and cairo do.
  loadGlyphFlags |= FT_LOAD_IGNORE_GLOBAL_ADVANCE_WIDTH;
  loadGlyphFlags |= FT_LOAD_TARGET_NORMAL;
  auto face = typeface->face;
  if (FT_HAS_COLOR(face)) {
    loadGlyphFlags |= FT_LOAD_COLOR;
  }

  auto err = FT_New_Size(face, &ftSize);
  if (err != FT_Err_Ok) {
    LOGE("FT_New_Size(%s) failed.", face->family_name);
    return;
  }
  err = FT_Activate_Size(ftSize);
  if (err != FT_Err_Ok) {
    LOGE("FT_Activate_Size(%s) failed.", face->family_name);
    return;
  }
  if (textSize < 0.0f) {
    textSize = 0.0f;
  }
  if (FloatNearlyZero(textSize) || !FloatsAreFinite(&textSize, 1)) {
    textSize = 1.0f;
    extraScale.set(0.0f, 0.0f);
  }
  auto textScale = FloatToFDot6(textSize);
  if (FT_IS_SCALABLE(face)) {
    err = FT_Set_Char_Size(face, textScale, textScale, 72, 72);
    if (err != FT_Err_Ok) {
      LOGE("FT_Set_CharSize(%s, %f, %f) failed.", face->family_name, textScale, textScale);
      return;
    }
    // Adjust the matrix to reflect the actually chosen scale.
    // FreeType currently does not allow requesting sizes less than 1, this allows for scaling.
    // Don't do this at all sizes as that will interfere with hinting.
    if (textSize < 1) {
      auto unitsPerEm = static_cast<float>(face->units_per_EM);
      const auto& metrics = face->size->metrics;
      auto xPpem = unitsPerEm * FTFixedToFloat(metrics.x_scale) / 64.0f;
      auto yPpem = unitsPerEm * FTFixedToFloat(metrics.y_scale) / 64.0f;
      extraScale.x *= textSize / xPpem;
      extraScale.y *= textSize / yPpem;
    }
  } else if (FT_HAS_FIXED_SIZES(face)) {
    strikeIndex = ChooseBitmapStrike(face, textScale);
    if (strikeIndex == -1) {
      LOGE("No glyphs for font \"%s\" size %f.\n", face->family_name, textSize);
      return;
    }

    err = FT_Select_Size(face, strikeIndex);
    if (err != 0) {
      LOGE("FT_Select_Size(%s, %d) failed.", face->family_name, strikeIndex);
      strikeIndex = -1;
      return;
    }

    // Adjust the matrix to reflect the actually chosen scale.
    // It is likely that the ppem chosen was not the one requested; this allows for scaling.
    extraScale.x *= textSize / static_cast<float>(face->size->metrics.x_ppem);
    extraScale.y *= textSize / static_cast<float>(face->size->metrics.y_ppem);

    // FreeType documentation says:
    // FT_LOAD_NO_BITMAP -- Ignore bitmap strikes when loading.
    // Bitmap-only fonts ignore this flag.
    //
    // However, in FreeType 2.5.1 color bitmap-only fonts do not ignore this flag.
    // Force this flag off for bitmap-only fonts.
    loadGlyphFlags &= ~FT_LOAD_NO_BITMAP;
  }
}

FTScalerContext::~FTScalerContext() {
  if (ftSize) {
    std::lock_guard<std::mutex> autoLock(typeface->locker);
    FT_Done_Size(ftSize);
  }
}

int FTScalerContext::setupSize(bool fauxItalic) {
  FT_Error err = FT_Activate_Size(ftSize);
  if (err != 0) {
    return err;
  }
  auto matrix = getExtraMatrix(fauxItalic);
  FT_Matrix matrix22 = {
      FloatToFTFixed(matrix.getScaleX()),
      FloatToFTFixed(-matrix.getSkewX()),
      FloatToFTFixed(-matrix.getSkewY()),
      FloatToFTFixed(matrix.getScaleY()),
  };
  FT_Set_Transform(typeface->face, &matrix22, nullptr);
  return 0;
}

FontMetrics FTScalerContext::generateFontMetrics() {
  std::lock_guard<std::mutex> autoLock(typeface->locker);
  FontMetrics metrics = {};
  getFontMetricsInternal(&metrics);
  return metrics;
}

void FTScalerContext::getFontMetricsInternal(FontMetrics* metrics) {
  if (fontMetrics != nullptr) {
    *metrics = *fontMetrics;
    return;
  }
  if (setupSize(false)) {
    return;
  }
  auto face = typeface->face;
  auto upem = static_cast<float>(FTTypeface::GetUnitsPerEm(face));

  // use the os/2 table as a source of reasonable defaults.
  auto xHeight = 0.0f;
  auto capHeight = 0.0f;
  auto* os2 = static_cast<TT_OS2*>(FT_Get_Sfnt_Table(face, FT_SFNT_OS2));
  if (os2) {
    xHeight = static_cast<float>(os2->sxHeight) / upem * textSize;
    if (os2->version != 0xFFFF && os2->version >= 2) {
      capHeight = static_cast<float>(os2->sCapHeight) / upem * textSize;
    }
  }

  // pull from format-specific metrics as needed
  float ascent;
  float descent;
  float leading;
  float xmin;
  float xmax;
  float ymin;
  float ymax;
  float underlineThickness;
  float underlinePosition;
  if (face->face_flags & FT_FACE_FLAG_SCALABLE) {  // scalable outline font
    // FreeType will always use HHEA metrics if they're not zero.
    // It completely ignores the OS/2 fsSelection::UseTypoMetrics bit.
    // It also ignores the VDMX tables, which are also of interest here
    // (and override everything else when they apply).
    static const int kUseTypoMetricsMask = (1 << 7);
    if (os2 && os2->version != 0xFFFF && (os2->fsSelection & kUseTypoMetricsMask)) {
      ascent = -static_cast<float>(os2->sTypoAscender) / upem;
      descent = -static_cast<float>(os2->sTypoDescender) / upem;
      leading = static_cast<float>(os2->sTypoLineGap) / upem;
    } else {
      ascent = -static_cast<float>(face->ascender) / upem;
      descent = -static_cast<float>(face->descender) / upem;
      leading = static_cast<float>(face->height + (face->descender - face->ascender)) / upem;
    }
    xmin = static_cast<float>(face->bbox.xMin) / upem;
    xmax = static_cast<float>(face->bbox.xMax) / upem;
    ymin = -static_cast<float>(face->bbox.yMin) / upem;
    ymax = -static_cast<float>(face->bbox.yMax) / upem;
    underlineThickness = static_cast<float>(face->underline_thickness) / upem;
    underlinePosition = -(static_cast<float>(face->underline_position) +
                          static_cast<float>(face->underline_thickness) / 2) /
                        upem;

    // we may be able to synthesize x_height and cap_height from outline
    if (xHeight == 0.f) {
      FT_BBox bbox;
      if (getCBoxForLetter('x', &bbox)) {
        xHeight = static_cast<float>(bbox.yMax) / 64.0f;
      }
    }
    if (capHeight == 0.f) {
      FT_BBox bbox;
      if (getCBoxForLetter('H', &bbox)) {
        capHeight = static_cast<float>(bbox.yMax) / 64.0f;
      }
    }
  } else if (strikeIndex != -1) {  // bitmap strike metrics
    auto xppem = static_cast<float>(face->size->metrics.x_ppem);
    auto yppem = static_cast<float>(face->size->metrics.y_ppem);
    ascent = -static_cast<float>(face->size->metrics.ascender) / (yppem * 64.0f);
    descent = -static_cast<float>(face->size->metrics.descender) / (yppem * 64.0f);
    leading = (static_cast<float>(face->size->metrics.height) / (yppem * 64.0f)) + ascent - descent;

    xmin = 0.0f;
    xmax = static_cast<float>(face->available_sizes[strikeIndex].width) / xppem;
    ymin = descent;
    ymax = ascent;

    underlineThickness = 0;
    underlinePosition = 0;

    auto* post = static_cast<TT_Postscript*>(FT_Get_Sfnt_Table(face, FT_SFNT_POST));
    if (post) {
      underlineThickness = static_cast<float>(post->underlineThickness) / upem;
      underlinePosition = -static_cast<float>(post->underlinePosition) / upem;
    }
  } else {
    return;
  }

  // synthesize elements that were not provided by the os/2 table or format-specific metrics
  if (xHeight == 0.f) {
    xHeight = -ascent * textSize;
  }
  if (capHeight == 0.f) {
    capHeight = -ascent * textSize;
  }

  // disallow negative line spacing
  if (leading < 0.0f) {
    leading = 0.0f;
  }
  metrics->top = ymax * textSize;
  metrics->ascent = ascent * textSize;
  metrics->descent = descent * textSize;
  metrics->bottom = ymin * textSize;
  metrics->leading = leading * textSize;
  metrics->xMin = xmin * textSize;
  metrics->xMax = xmax * textSize;
  metrics->xHeight = xHeight;
  metrics->capHeight = capHeight;
  metrics->underlineThickness = underlineThickness * textSize;
  metrics->underlinePosition = underlinePosition * textSize;
  fontMetrics = std::make_unique<FontMetrics>();
  *fontMetrics = *metrics;
}

bool FTScalerContext::getCBoxForLetter(char letter, FT_BBox* bbox) {
  auto face = typeface->face;
  const auto glyph_id = FT_Get_Char_Index(face, static_cast<FT_ULong>(letter));
  if (glyph_id == 0) {
    return false;
  }
  if (FT_Load_Glyph(face, glyph_id, loadGlyphFlags) != FT_Err_Ok) {
    return false;
  }
  FT_Outline_Get_CBox(&face->glyph->outline, bbox);
  return true;
}

class FTGeometrySink {
 public:
  explicit FTGeometrySink(Path* path) : path(path) {
  }

  static int Move(const FT_Vector* pt, void* ctx) {
    auto& self = *static_cast<FTGeometrySink*>(ctx);
    if (self.started) {
      self.path->close();
      self.started = false;
    }
    self.current = *pt;
    return 0;
  }

  static int Line(const FT_Vector* pt, void* ctx) {
    auto& self = *static_cast<FTGeometrySink*>(ctx);
    if (self.currentIsNot(pt)) {
      self.goingTo(pt);
      self.path->lineTo(FDot6ToFloat(pt->x), -FDot6ToFloat(pt->y));
    }
    return 0;
  }

  static int Conic(const FT_Vector* pt0, const FT_Vector* pt1, void* ctx) {
    auto& self = *static_cast<FTGeometrySink*>(ctx);
    if (self.currentIsNot(pt0) || self.currentIsNot(pt1)) {
      self.goingTo(pt1);
      self.path->quadTo(FDot6ToFloat(pt0->x), -FDot6ToFloat(pt0->y), FDot6ToFloat(pt1->x),
                        -FDot6ToFloat(pt1->y));
    }
    return 0;
  }

  static int Cubic(const FT_Vector* pt0, const FT_Vector* pt1, const FT_Vector* pt2, void* ctx) {
    auto& self = *static_cast<FTGeometrySink*>(ctx);
    if (self.currentIsNot(pt0) || self.currentIsNot(pt1) || self.currentIsNot(pt2)) {
      self.goingTo(pt2);
      self.path->cubicTo(FDot6ToFloat(pt0->x), -FDot6ToFloat(pt0->y), FDot6ToFloat(pt1->x),
                         -FDot6ToFloat(pt1->y), FDot6ToFloat(pt2->x), -FDot6ToFloat(pt2->y));
    }
    return 0;
  }

 private:
  bool currentIsNot(const FT_Vector* pt) const {
    return current.x != pt->x || current.y != pt->y;
  }

  void goingTo(const FT_Vector* pt) {
    if (!started) {
      started = true;
      path->moveTo(FDot6ToFloat(current.x), -FDot6ToFloat(current.y));
    }
    current = *pt;
  }

  Path* path;
  bool started = false;
  FT_Vector current = {0, 0};
};

static bool GenerateGlyphPath(FT_Face face, Path* path) {
  static constexpr FT_Outline_Funcs gFuncs{
      /*move_to =*/FTGeometrySink::Move,
      /*line_to =*/FTGeometrySink::Line,
      /*conic_to =*/FTGeometrySink::Conic,
      /*cubic_to =*/FTGeometrySink::Cubic,
      /*shift = */ 0,
      /*delta =*/0,
  };
  FTGeometrySink sink(path);
  auto err = FT_Outline_Decompose(&face->glyph->outline, &gFuncs, &sink);
  if (err != FT_Err_Ok) {
    path->reset();
    return false;
  }
  path->close();
  return true;
}

bool FTScalerContext::generatePath(GlyphID glyphID, bool fauxBold, bool fauxItalic, Path* path) {
  std::lock_guard<std::mutex> autoLock(typeface->locker);
  auto face = typeface->face;
  // FT_IS_SCALABLE is documented to mean the face contains outline glyphs.
  if (!FT_IS_SCALABLE(face) || setupSize(fauxItalic)) {
    path->reset();
    return false;
  }
  auto flags = loadGlyphFlags;
  flags |= FT_LOAD_NO_BITMAP;  // ignore embedded bitmaps so we're sure to get the outline
  flags &= ~FT_LOAD_RENDER;    // don't scan convert (we just want the outline)

  auto err = FT_Load_Glyph(face, glyphID, flags);
  if (err != FT_Err_Ok || face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) {
    path->reset();
    return false;
  }
  if (fauxBold) {
    ApplyEmbolden(face, face->glyph, glyphID, loadGlyphFlags);
  }
  if (!GenerateGlyphPath(face, path)) {
    path->reset();
    return false;
  }
  return true;
}

void FTScalerContext::getBBoxForCurrentGlyph(FT_BBox* bbox) {
  auto face = typeface->face;
  FT_Outline_Get_CBox(&face->glyph->outline, bbox);

  // outset the box to integral boundaries
  bbox->xMin &= ~63;
  bbox->yMin &= ~63;
  bbox->xMax = (bbox->xMax + 63) & ~63;
  bbox->yMax = (bbox->yMax + 63) & ~63;
}

GlyphMetrics FTScalerContext::generateGlyphMetrics(GlyphID glyphID, bool fauxBold,
                                                   bool fauxItalic) {
  std::lock_guard<std::mutex> autoLock(typeface->locker);
  GlyphMetrics metrics;
  if (setupSize(fauxItalic)) {
    return metrics;
  }
  auto glyphFlags = loadGlyphFlags | static_cast<FT_Int32>(FT_LOAD_BITMAP_METRICS_ONLY);
  auto face = typeface->face;
  auto err = FT_Load_Glyph(face, glyphID, glyphFlags);
  if (err != FT_Err_Ok) {
    return metrics;
  }
  if (fauxBold) {
    ApplyEmbolden(face, face->glyph, glyphID, glyphFlags);
  }
  if (face->glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
    using FT_PosLimits = std::numeric_limits<FT_Pos>;
    FT_BBox bounds = {FT_PosLimits::max(), FT_PosLimits::max(), FT_PosLimits::min(),
                      FT_PosLimits::min()};
    if (0 < face->glyph->outline.n_contours) {
      getBBoxForCurrentGlyph(&bounds);
    } else {
      bounds = {0, 0, 0, 0};
    }
    // Round out, no longer dot6.
    bounds.xMin = FDot6Floor(bounds.xMin);
    bounds.yMin = FDot6Floor(bounds.yMin);
    bounds.xMax = FDot6Ceil(bounds.xMax);
    bounds.yMax = FDot6Ceil(bounds.yMax);

    FT_Pos width = bounds.xMax - bounds.xMin;
    FT_Pos height = bounds.yMax - bounds.yMin;
    FT_Pos top = -bounds.yMax;  // Freetype y-up, We y-down.
    FT_Pos left = bounds.xMin;

    metrics.width = static_cast<float>(width);
    metrics.height = static_cast<float>(height);
    metrics.top = static_cast<float>(top);
    metrics.left = static_cast<float>(left);
  } else if (face->glyph->format == FT_GLYPH_FORMAT_BITMAP) {
    auto rect = Rect::MakeXYWH(static_cast<float>(face->glyph->bitmap_left),
                               -static_cast<float>(face->glyph->bitmap_top),
                               static_cast<float>(face->glyph->bitmap.width),
                               static_cast<float>(face->glyph->bitmap.rows));
    auto matrix = getExtraMatrix(fauxItalic);
    matrix.mapRect(&rect);
    rect.roundOut();
    metrics.width = static_cast<float>(rect.width());
    metrics.height = static_cast<float>(rect.height());
    metrics.top = static_cast<float>(rect.top);
    metrics.left = static_cast<float>(rect.left);
  } else {
    LOGE("unknown glyph format");
    return metrics;
  }

  metrics.advanceX = FDot6ToFloat(face->glyph->advance.x);
  metrics.advanceY = FDot6ToFloat(face->glyph->advance.y);
  return metrics;
}

float FTScalerContext::getAdvance(GlyphID glyphID, bool verticalText) {
  std::lock_guard<std::mutex> autoLock(typeface->locker);
  return getAdvanceInternal(glyphID, verticalText);
}

float FTScalerContext::getAdvanceInternal(tgfx::GlyphID glyphID, bool verticalText) {
  if (setupSize(false)) {
    return 0;
  }
  auto face = typeface->face;
  auto glyphFlags = loadGlyphFlags | static_cast<FT_Int32>(FT_LOAD_BITMAP_METRICS_ONLY);
  if (verticalText) {
    glyphFlags |= FT_LOAD_VERTICAL_LAYOUT;
  }
  auto err = FT_Load_Glyph(face, glyphID, glyphFlags);
  if (err != FT_Err_Ok) {
    return 0;
  }
  return verticalText ? FDot6ToFloat(face->glyph->advance.y) : FDot6ToFloat(face->glyph->advance.x);
}

Point FTScalerContext::getVerticalOffset(tgfx::GlyphID glyphID) {
  std::lock_guard<std::mutex> autoLock(typeface->locker);
  if (glyphID == 0) {
    return Point::Zero();
  }
  FontMetrics metrics = {};
  getFontMetricsInternal(&metrics);
  auto advanceX = getAdvanceInternal(glyphID);
  return {-advanceX * 0.5f, metrics.capHeight};
}

static gfx::skcms_PixelFormat ToPixelFormat(ColorType colorType) {
  switch (colorType) {
    case ColorType::ALPHA_8:
      return gfx::skcms_PixelFormat_A_8;
    case ColorType::BGRA_8888:
      return gfx::skcms_PixelFormat_BGRA_8888;
    default:
      return gfx::skcms_PixelFormat_RGBA_8888;
  }
}

std::shared_ptr<ImageBuffer> CopyFTBitmap(const FT_Bitmap& ftBitmap) {
  auto alphaOnly = ftBitmap.pixel_mode == FT_PIXEL_MODE_GRAY;
  Bitmap bitmap(static_cast<int>(ftBitmap.width), static_cast<int>(ftBitmap.rows), alphaOnly);
  if (bitmap.isEmpty()) {
    return nullptr;
  }
  auto width = ftBitmap.width;
  auto height = ftBitmap.rows;
  auto src = reinterpret_cast<const uint8_t*>(ftBitmap.buffer);
  // FT_Bitmap::pitch is an int and allowed to be negative.
  auto srcRB = ftBitmap.pitch;
  auto srcFormat = ftBitmap.pixel_mode == FT_PIXEL_MODE_GRAY ? gfx::skcms_PixelFormat_A_8
                                                             : gfx::skcms_PixelFormat_BGRA_8888;
  Pixmap bm(bitmap);
  auto dst = static_cast<uint8_t*>(bm.writablePixels());
  auto dstRB = bm.rowBytes();
  auto dstFormat = ToPixelFormat(bm.colorType());
  for (size_t i = 0; i < height; i++) {
    gfx::skcms_Transform(src, srcFormat, gfx::skcms_AlphaFormat_PremulAsEncoded, nullptr, dst,
                         dstFormat, gfx::skcms_AlphaFormat_PremulAsEncoded, nullptr, width);
    src += srcRB;
    dst += dstRB;
  }
  return bitmap.makeBuffer();
}

std::shared_ptr<ImageBuffer> FTScalerContext::generateImage(GlyphID glyphID, bool fauxItalic,
                                                            Matrix* matrix) {
  std::lock_guard<std::mutex> autoLock(typeface->locker);
  if (setupSize(fauxItalic)) {
    return nullptr;
  }
  auto face = typeface->face;
  auto glyphFlags = loadGlyphFlags;
  glyphFlags |= FT_LOAD_RENDER;
  glyphFlags &= ~FT_LOAD_NO_BITMAP;
  auto err = FT_Load_Glyph(face, glyphID, glyphFlags);
  if (err != FT_Err_Ok || face->glyph->format != FT_GLYPH_FORMAT_BITMAP) {
    return nullptr;
  }
  auto ftBitmap = face->glyph->bitmap;
  if (ftBitmap.pixel_mode != FT_PIXEL_MODE_BGRA && ftBitmap.pixel_mode != FT_PIXEL_MODE_GRAY) {
    return nullptr;
  }
  if (matrix) {
    matrix->setTranslate(static_cast<float>(face->glyph->bitmap_left),
                         -static_cast<float>(face->glyph->bitmap_top));
    auto extraMatrix = getExtraMatrix(fauxItalic);
    matrix->postConcat(extraMatrix);
  }
  return CopyFTBitmap(ftBitmap);
}

Matrix FTScalerContext::getExtraMatrix(bool fauxItalic) {
  auto matrix = Matrix::MakeScale(extraScale.x, extraScale.y);
  if (fauxItalic) {
    matrix.postSkew(ITALIC_SKEW, 0);
  }
  return matrix;
}
}  // namespace tgfx
