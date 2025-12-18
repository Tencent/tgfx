/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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
#include "ft2build.h"
#include FT_BITMAP_H
#include FT_OUTLINE_H
#include FT_SIZES_H
#include FT_TRUETYPE_TABLES_H
#include "FTRasterTarget.h"
#include "FTUtil.h"
#include "core/utils/ClearPixels.h"
#include "core/utils/ColorSpaceHelper.h"
#include "core/utils/GammaCorrection.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "core/utils/USE.h"
#include "skcms.h"
#include "tgfx/core/Buffer.h"
#include "tgfx/core/Pixmap.h"
#include "tgfx/core/UTF.h"

#if defined(__ANDROID__) || defined(ANDROID)
#include "platform/android/GlyphRenderer.h"
#endif

namespace tgfx {
//  See http://freetype.sourceforge.net/freetype2/docs/reference/ft2-bitmap_handling.html#FT_Bitmap_Embolden
//  This value was chosen by eyeballing the result in Firefox and trying to match it.
static constexpr FT_Pos BITMAP_EMBOLDEN_STRENGTH = 1 << 6;
static constexpr int OUTLINE_EMBOLDEN_DIVISOR = 24;

static constexpr FT_UInt DefaultResolutionInDPI = 72;

// When FT_Set_Char_Size is called, the ppem value is recalculated using the formula:
// ppem = textSize * DPI / 72.
// In FreeType, ppem is stored as an unsigned short, so if textSize exceeds 65535, ppem will overflow.
// To prevent this, we cap textSize at 65535. For the ppem limit in the source code, check here:
// https://github.com/freetype/freetype/blob/VER-2-13-3/src/base/ftobjs.c/#L3359
static constexpr float MaxTextSize = 65535.f * 72.f / static_cast<float>(DefaultResolutionInDPI);

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

static void RenderOutLineGlyph(FT_Face face, const ImageInfo& dstInfo, void* dstPixels) {
  auto buffer = static_cast<unsigned char*>(dstPixels);
  auto pitch = static_cast<int>(dstInfo.rowBytes());
  FT_Raster_Params params;
  params.flags = FT_RASTER_FLAG_CLIP | FT_RASTER_FLAG_AA;
#if defined(TGFX_USE_TEXT_GAMMA_CORRECTION)
  auto rows = dstInfo.height();
  params.flags |= FT_RASTER_FLAG_DIRECT;
  params.gray_spans = GraySpanFunc;
  FTRasterTarget target = {buffer + (rows - 1) * pitch, pitch,
                           GammaCorrection::GammaTable().data()};
  params.user = &target;
#else
  FT_Bitmap bitmap;
  bitmap.width = static_cast<unsigned>(dstInfo.width());
  bitmap.rows = static_cast<unsigned>(dstInfo.height());
  bitmap.pitch = pitch;
  bitmap.buffer = buffer;
  bitmap.pixel_mode = FT_PIXEL_MODE_GRAY;
  bitmap.num_grays = 256;
  params.target = &bitmap;
#endif
  params.clip_box = {0, 0, static_cast<FT_Pos>(dstInfo.width()),
                     static_cast<FT_Pos>(dstInfo.height())};
  auto outline = &face->glyph->outline;
  FT_BBox bbox;
  FT_Outline_Get_CBox(outline, &bbox);
  // outset the box to integral boundaries
  bbox.xMin &= ~63;
  bbox.yMin &= ~63;
  FT_Outline_Translate(outline, -bbox.xMin, -bbox.yMin);
  FT_Outline_Render(face->glyph->library, outline, &params);

  if (NeedConvertColorSpace(ColorSpace::SRGB(), dstInfo.colorSpace())) {
    ConvertColorSpaceInPlace(dstInfo.width(), dstInfo.height(), dstInfo.colorType(),
                             dstInfo.alphaType(), dstInfo.rowBytes(), ColorSpace::SRGB(),
                             dstInfo.colorSpace(), dstPixels);
  }
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

FTScalerContext::FTScalerContext(std::shared_ptr<Typeface> typeFace, float size)
    : ScalerContext(std::move(typeFace), size), textScale(size) {
  backingSize = textSize;
  loadGlyphFlags |= FT_LOAD_NO_BITMAP;
  // Always using FT_LOAD_IGNORE_GLOBAL_ADVANCE_WIDTH to get correct
  // advances, as fontconfig and cairo do.
  loadGlyphFlags |= FT_LOAD_IGNORE_GLOBAL_ADVANCE_WIDTH;
  loadGlyphFlags |= FT_LOAD_TARGET_NORMAL;
  auto face = ftTypeface()->face;
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
  if (FloatNearlyZero(textScale) || !FloatsAreFinite(&textScale, 1)) {
    textScale = 1.0f;
    extraScale.set(0.0f, 0.0f);
  }
  auto textScaleDot6 = FloatToFDot6(textScale);
  if (FT_IS_SCALABLE(face)) {
    bool exceedScaleLimit = false;
    if (textScale > MaxTextSize) {
      textScaleDot6 = FloatToFDot6(MaxTextSize);
      exceedScaleLimit = true;
    }
    err = FT_Set_Char_Size(face, textScaleDot6, textScaleDot6, DefaultResolutionInDPI,
                           DefaultResolutionInDPI);
    if (err != FT_Err_Ok) {
      LOGE("FT_Set_CharSize(%s, %f, %f) failed.", face->family_name, textScaleDot6, textScaleDot6);
      return;
    }
    // Adjust the matrix to reflect the actually chosen scale.
    // FreeType currently does not allow requesting sizes less than 1, this allows for scaling.
    // Don't do this at all sizes as that will interfere with hinting.
    if (textScale < 1) {
      auto unitsPerEm = static_cast<float>(face->units_per_EM);
      const auto& metrics = face->size->metrics;
      auto xPpem = unitsPerEm * FTFixedToFloat(metrics.x_scale) / 64.0f;
      auto yPpem = unitsPerEm * FTFixedToFloat(metrics.y_scale) / 64.0f;
      extraScale.x *= textScale / xPpem;
      extraScale.y *= textScale / yPpem;
    } else if (exceedScaleLimit) {
      extraScale *= textScale / MaxTextSize;
    }
  } else if (FT_HAS_FIXED_SIZES(face)) {
    strikeIndex = ChooseBitmapStrike(face, textScaleDot6);
    if (strikeIndex == -1) {
      LOGE("No glyphs for font \"%s\" size %f.\n", face->family_name, textScaleDot6);
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
    extraScale.x *= textScale / static_cast<float>(face->size->metrics.x_ppem);
    extraScale.y *= textScale / static_cast<float>(face->size->metrics.y_ppem);

    // FreeType documentation says:
    // FT_LOAD_NO_BITMAP -- Ignore bitmap strikes when loading.
    // Bitmap-only fonts ignore this flag.
    //
    // However, in FreeType 2.5.1 color bitmap-only fonts do not ignore this flag.
    // Force this flag off for bitmap-only fonts.
    loadGlyphFlags &= ~FT_LOAD_NO_BITMAP;

    backingSize = FDot6ToFloat(face->available_sizes[strikeIndex].y_ppem);
  }
}

FTScalerContext::~FTScalerContext() {
  if (ftSize) {
    std::lock_guard<std::mutex> autoLock(ftTypeface()->locker);
    FT_Done_Size(ftSize);
  }
}

int FTScalerContext::setupSize(bool fauxItalic) const {
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
  FT_Set_Transform(ftTypeface()->face, &matrix22, nullptr);
  return 0;
}

FontMetrics FTScalerContext::getFontMetrics() const {
  std::lock_guard<std::mutex> autoLock(ftTypeface()->locker);
  FontMetrics metrics = {};
  if (setupSize(false)) {
    return metrics;
  }
  getFontMetricsInternal(&metrics);
  return metrics;
}

void FTScalerContext::getFontMetricsInternal(FontMetrics* metrics) const {
  auto face = ftTypeface()->face;
  auto upem = static_cast<float>(ftTypeface()->unitsPerEmInternal());

  // use the os/2 table as a source of reasonable defaults.
  auto xHeight = 0.0f;
  auto capHeight = 0.0f;
  auto os2 = static_cast<TT_OS2*>(FT_Get_Sfnt_Table(face, FT_SFNT_OS2));
  if (os2) {
    xHeight = static_cast<float>(os2->sxHeight) / upem * textScale;
    if (os2->version != 0xFFFF && os2->version >= 2) {
      capHeight = static_cast<float>(os2->sCapHeight) / upem * textScale;
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
    static const int UseTypoMetricsMask = (1 << 7);
    if (os2 && os2->version != 0xFFFF && (os2->fsSelection & UseTypoMetricsMask)) {
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

    auto post = static_cast<TT_Postscript*>(FT_Get_Sfnt_Table(face, FT_SFNT_POST));
    if (post) {
      underlineThickness = static_cast<float>(post->underlineThickness) / upem;
      underlinePosition = -static_cast<float>(post->underlinePosition) / upem;
    }
  } else {
    return;
  }

  // synthesize elements that were not provided by the os/2 table or format-specific metrics
  if (xHeight == 0.f) {
    xHeight = -ascent * textScale;
  }
  if (capHeight == 0.f) {
    capHeight = -ascent * textScale;
  }

  // disallow negative line spacing
  if (leading < 0.0f) {
    leading = 0.0f;
  }
  metrics->top = ymax * textScale;
  metrics->ascent = ascent * textScale;
  metrics->descent = descent * textScale;
  metrics->bottom = ymin * textScale;
  metrics->leading = leading * textScale;
  metrics->xMin = xmin * textScale;
  metrics->xMax = xmax * textScale;
  metrics->xHeight = xHeight;
  metrics->capHeight = capHeight;
  metrics->underlineThickness = underlineThickness * textScale;
  metrics->underlinePosition = underlinePosition * textScale;
}

bool FTScalerContext::getCBoxForLetter(char letter, FT_BBox* bbox) const {
  auto face = ftTypeface()->face;
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

bool FTScalerContext::generatePath(GlyphID glyphID, bool fauxBold, bool fauxItalic,
                                   Path* path) const {
  std::lock_guard<std::mutex> autoLock(ftTypeface()->locker);
  auto face = ftTypeface()->face;
  if (!loadOutlineGlyph(face, glyphID, fauxBold, fauxItalic)) {
    path->reset();
    return false;
  }
  if (!GenerateGlyphPath(face, path)) {
    path->reset();
    return false;
  }
  return true;
}

void FTScalerContext::getBBoxForCurrentGlyph(FT_BBox* bbox) const {
  auto face = ftTypeface()->face;
  FT_Outline_Get_CBox(&face->glyph->outline, bbox);

  // outset the box to integral boundaries
  bbox->xMin &= ~63;
  bbox->yMin &= ~63;
  bbox->xMax = (bbox->xMax + 63) & ~63;
  bbox->yMax = (bbox->yMax + 63) & ~63;
}

Rect FTScalerContext::getBounds(tgfx::GlyphID glyphID, bool fauxBold, bool fauxItalic) const {
#if defined(__ANDROID__) || defined(ANDROID)
  if (ftTypeface()->isColorVector() && GlyphRenderer::IsAvailable()) {
    Rect rect = {};
    if (MeasureCOLRv1Glyph(glyphID, &rect)) {
      return rect;
    }
  }
#endif
  std::lock_guard<std::mutex> autoLock(ftTypeface()->locker);
  Rect bounds = {};
  if (setupSize(fauxItalic)) {
    return bounds;
  }
  auto glyphFlags = loadGlyphFlags | static_cast<FT_Int32>(FT_LOAD_BITMAP_METRICS_ONLY);
  auto face = ftTypeface()->face;
  auto err = FT_Load_Glyph(face, glyphID, glyphFlags);
  if (err != FT_Err_Ok) {
    return bounds;
  }
  if (fauxBold) {
    ApplyEmbolden(face, face->glyph, glyphID, glyphFlags);
  }
  if (face->glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
    using FT_PosLimits = std::numeric_limits<FT_Pos>;
    FT_BBox rect = {FT_PosLimits::max(), FT_PosLimits::max(), FT_PosLimits::min(),
                    FT_PosLimits::min()};
    if (0 < face->glyph->outline.n_contours) {
      getBBoxForCurrentGlyph(&rect);
    } else {
      rect = {0, 0, 0, 0};
    }
    // Round out, no longer dot6.
    rect.xMin = FDot6Floor(rect.xMin);
    rect.yMin = FDot6Floor(rect.yMin);
    rect.xMax = FDot6Ceil(rect.xMax);
    rect.yMax = FDot6Ceil(rect.yMax);

    FT_Pos width = rect.xMax - rect.xMin;
    FT_Pos height = rect.yMax - rect.yMin;
    FT_Pos top = -rect.yMax;  // Freetype y-up, We y-down.
    FT_Pos left = rect.xMin;

    bounds.setXYWH(static_cast<float>(left), static_cast<float>(top), static_cast<float>(width),
                   static_cast<float>(height));
  } else if (face->glyph->format == FT_GLYPH_FORMAT_BITMAP) {
    bounds.setXYWH(static_cast<float>(face->glyph->bitmap_left),
                   -static_cast<float>(face->glyph->bitmap_top),
                   static_cast<float>(face->glyph->bitmap.width),
                   static_cast<float>(face->glyph->bitmap.rows));
    auto matrix = getExtraMatrix(fauxItalic);
    matrix.mapRect(&bounds);
    bounds.roundOut();
  } else {
    LOGE("FTScalerContext::getBounds() unknown glyph format!");
  }
  return bounds;
}

float FTScalerContext::getAdvance(GlyphID glyphID, bool verticalText) const {
  std::lock_guard<std::mutex> autoLock(ftTypeface()->locker);
  if (setupSize(false)) {
    return 0;
  }
  return getAdvanceInternal(glyphID, verticalText);
}

float FTScalerContext::getAdvanceInternal(GlyphID glyphID, bool verticalText) const {
  auto face = ftTypeface()->face;
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

Point FTScalerContext::getVerticalOffset(GlyphID glyphID) const {
  std::lock_guard<std::mutex> autoLock(ftTypeface()->locker);
  if (glyphID == 0 || setupSize(false)) {
    return {};
  }
  FontMetrics metrics = {};
  getFontMetricsInternal(&metrics);
  auto advanceX = getAdvanceInternal(glyphID);
  return {-advanceX * 0.5f, metrics.capHeight};
}

Rect FTScalerContext::getImageTransform(GlyphID glyphID, bool fauxBold, const Stroke* stroke,
                                        Matrix* matrix) const {
  if (!hasColor() && stroke != nullptr) {
    return {};
  }
  if (!hasColor()) {
    const auto bounds = getBounds(glyphID, fauxBold, false);
    if (matrix) {
      matrix->setTranslate(bounds.x(), bounds.y());
    }
    return bounds;
  }
#if defined(__ANDROID__) || defined(ANDROID)
  if (ftTypeface()->isColorVector() && GlyphRenderer::IsAvailable()) {
    Rect rect = {};
    if (MeasureCOLRv1Glyph(glyphID, &rect)) {
      if (matrix) {
        matrix->setTranslate(rect.x(), rect.y());
      }
    }
    return rect;
  }
#endif
  std::lock_guard<std::mutex> autoLock(ftTypeface()->locker);
  auto glyphFlags = loadGlyphFlags | static_cast<FT_Int32>(FT_LOAD_BITMAP_METRICS_ONLY);
  glyphFlags &= ~FT_LOAD_NO_BITMAP;
  if (loadBitmapGlyph(glyphID, glyphFlags)) {
    auto face = ftTypeface()->face;
    if (matrix) {
      matrix->setTranslate(static_cast<float>(face->glyph->bitmap_left),
                           -static_cast<float>(face->glyph->bitmap_top));
      matrix->postScale(extraScale.x, extraScale.y);
    }
    return Rect::MakeXYWH(static_cast<float>(face->glyph->bitmap_left),
                          -static_cast<float>(face->glyph->bitmap_top),
                          static_cast<float>(face->glyph->bitmap.width),
                          static_cast<float>(face->glyph->bitmap.rows));
  }
  return {};
}

bool FTScalerContext::readPixels(GlyphID glyphID, bool fauxBold, const Stroke*,
                                 const ImageInfo& dstInfo, void* dstPixels,
                                 const Point& glyphOffset) const {
  if (dstInfo.isEmpty() || dstPixels == nullptr) {
    return false;
  }
  // Note: In the hasColor() function, freeType has an internal lock. Placing this method later
  // would cause repeated locking and lead to a deadlock.
  bool colorFont = hasColor();
  bool isColorVector = ftTypeface()->isColorVector();
#if defined(__ANDROID__) || defined(ANDROID)
  if (isColorVector && GlyphRenderer::IsAvailable()) {
    std::string text = getGlyphUTF8(glyphID);
    if (!text.empty()) {
      auto fontPath = ftTypeface()->fontPath();
      float offsetX = -glyphOffset.x;
      float offsetY = -glyphOffset.y;
      auto width = static_cast<int>(dstInfo.width());
      auto height = static_cast<int>(dstInfo.height());
      bool formatCompatible = dstInfo.colorType() == ColorType::RGBA_8888 &&
                              dstInfo.alphaType() == AlphaType::Unpremultiplied &&
                              (dstInfo.colorSpace() == nullptr || dstInfo.colorSpace()->isSRGB());
      if (formatCompatible) {
        return GlyphRenderer::RenderGlyph(fontPath, text, textSize, width, height, offsetX, offsetY,
                                          dstPixels);
      }
      auto srcInfo = ImageInfo::Make(width, height, ColorType::RGBA_8888,
                                     AlphaType::Unpremultiplied, 0, ColorSpace::SRGB());
      Buffer buffer{srcInfo.byteSize()};
      if (!GlyphRenderer::RenderGlyph(fontPath, text, textSize, width, height, offsetX, offsetY,
                                      buffer.data())) {
        return false;
      }
      Pixmap pixmap{srcInfo, buffer.data()};
      return pixmap.readPixels(dstInfo, dstPixels);
    }
    return false;
  }
#else
  USE(isColorVector);
  USE(glyphOffset);
#endif
  std::lock_guard<std::mutex> autoLock(ftTypeface()->locker);
  if (!colorFont) {
    auto face = ftTypeface()->face;
    if (!loadOutlineGlyph(face, glyphID, fauxBold, false)) {
      return false;
    }
    ClearPixels(dstInfo, dstPixels);
    RenderOutLineGlyph(face, dstInfo, dstPixels);
    return true;
  }
  auto glyphFlags = loadGlyphFlags;
  glyphFlags |= FT_LOAD_RENDER;
  glyphFlags &= ~FT_LOAD_NO_BITMAP;
  if (!loadBitmapGlyph(glyphID, glyphFlags)) {
    return false;
  }
  auto ftBitmap = ftTypeface()->face->glyph->bitmap;
  auto width = ftBitmap.width;
  auto height = ftBitmap.rows;
  auto src = reinterpret_cast<const uint8_t*>(ftBitmap.buffer);
  // FT_Bitmap::pitch is an int and allowed to be negative.
  auto srcRB = ftBitmap.pitch;
  auto srcFormat = ftBitmap.pixel_mode == FT_PIXEL_MODE_GRAY ? gfx::skcms_PixelFormat_A_8
                                                             : gfx::skcms_PixelFormat_BGRA_8888;
  auto srcProfile = *gfx::skcms_sRGB_profile();

  auto dst = static_cast<uint8_t*>(dstPixels);
  auto dstRB = dstInfo.rowBytes();
  auto dstFormat = ToPixelFormat(dstInfo.colorType());
  auto dstProfile = srcProfile;
  if (dstInfo.colorSpace() != nullptr) {
    dstProfile = ToSkcmsICCProfile(dstInfo.colorSpace());
  }
  for (size_t i = 0; i < height; i++) {
    gfx::skcms_Transform(src, srcFormat, gfx::skcms_AlphaFormat_PremulAsEncoded,
                         gfx::skcms_sRGB_profile(), dst, dstFormat,
                         gfx::skcms_AlphaFormat_PremulAsEncoded, &dstProfile, width);
    src += srcRB;
    dst += dstRB;
  }
  return true;
}

bool FTScalerContext::loadBitmapGlyph(GlyphID glyphID, FT_Int32 glyphFlags) const {
  if (setupSize(false)) {
    return false;
  }
  auto face = ftTypeface()->face;
  auto err = FT_Load_Glyph(face, glyphID, glyphFlags);
  if (err != FT_Err_Ok || face->glyph->format != FT_GLYPH_FORMAT_BITMAP) {
    return false;
  }
  auto ftBitmap = face->glyph->bitmap;
  return ftBitmap.pixel_mode == FT_PIXEL_MODE_BGRA || ftBitmap.pixel_mode == FT_PIXEL_MODE_GRAY;
}

Matrix FTScalerContext::getExtraMatrix(bool fauxItalic) const {
  auto matrix = Matrix::MakeScale(extraScale.x, extraScale.y);
  if (fauxItalic) {
    matrix.postSkew(ITALIC_SKEW, 0);
  }
  return matrix;
}

FTTypeface* FTScalerContext::ftTypeface() const {
  return static_cast<FTTypeface*>(typeface.get());
}

bool FTScalerContext::loadOutlineGlyph(FT_Face face, GlyphID glyphID, bool fauxBold,
                                       bool fauxItalic) const {
  // FT_IS_SCALABLE is documented to mean the face contains outline glyphs.
  if (!FT_IS_SCALABLE(face) || setupSize(fauxItalic)) {
    return false;
  }
  auto flags = loadGlyphFlags;
  flags |= FT_LOAD_NO_BITMAP;  // ignore embedded bitmaps so we're sure to get the outline
  flags &= ~FT_LOAD_RENDER;    // don't scan convert (we just want the outline)
  auto err = FT_Load_Glyph(face, glyphID, flags);
  if (err != FT_Err_Ok || face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) {
    return false;
  }
  if (fauxBold) {
    ApplyEmbolden(face, face->glyph, glyphID, flags);
  }
  return true;
}

#if defined(__ANDROID__) || defined(ANDROID)
bool FTScalerContext::MeasureCOLRv1Glyph(GlyphID glyphID, Rect* rect) const {
  std::string text = getGlyphUTF8(glyphID);
  if (text.empty()) {
    return false;
  }
  auto fontPath = ftTypeface()->fontPath();
  float bounds[4] = {};
  float advance = 0;
  if (!GlyphRenderer::MeasureText(fontPath, text, textSize, bounds, &advance)) {
    return false;
  }
  float width = std::max(bounds[2] - bounds[0], advance);
  float height = std::max(bounds[3] - bounds[1], textSize * 1.2f);
  *rect = Rect::MakeXYWH(bounds[0], bounds[1], width, height);
  return true;
}

std::string FTScalerContext::getGlyphUTF8(GlyphID glyphID) const {
  auto& map = ftTypeface()->getGlyphToUnicodeMap();
  Unichar unichar = glyphID < map.size() ? map[glyphID] : 0;
  return UTF::ToUTF8(unichar);
}
#endif
}  // namespace tgfx
