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
#include <include/core/SkPath.h>
#include <tgfx/core/Canvas.h>
#include <cmath>
#include "ft2build.h"
#include FT_BITMAP_H
#include FT_OUTLINE_H
#include FT_SIZES_H
#include FT_TRUETYPE_TABLES_H
#include "FTUtil.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "skcms.h"
#include "tgfx/core/Pixmap.h"

namespace tgfx {
//  See http://freetype.sourceforge.net/freetype2/docs/reference/ft2-bitmap_handling.html#FT_Bitmap_Embolden
//  This value was chosen by eyeballing the result in Firefox and trying to match it.
static constexpr FT_Pos BITMAP_EMBOLDEN_STRENGTH = 1 << 6;
static constexpr int OUTLINE_EMBOLDEN_DIVISOR = 24;

// likely and unlikely macros
#if defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(__clang__)
#define _likely_(x) __builtin_expect(x, 1)
#define _unlikely_(x) __builtin_expect(x, 0)
#else
#define _likely_(x) (x)
#define _unlikely_(x) (x)
#endif

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

std::shared_ptr<ScalerContext> ScalerContext::CreateNew(std::shared_ptr<Typeface> typeface,
                                                        float size) {
  DEBUG_ASSERT(typeface != nullptr);
  return std::make_shared<FTScalerContext>(std::move(typeface), size);
}

void _wymum(uint64_t* A, uint64_t* B) {
#if defined(__SIZEOF_INT128__)
  __uint128_t r = *A;
  r *= *B;
  *A = (uint64_t)r;
  *B = (uint64_t)(r >> 64);
#elif defined(_MSC_VER) && defined(_M_X64)
  *A = _umul128(*A, *B, B);
#else
  uint64_t ha = *A >> 32, hb = *B >> 32, la = (uint32_t)*A, lb = (uint32_t)*B, hi, lo;
  uint64_t rh = ha * hb, rm0 = ha * lb, rm1 = hb * la, rl = la * lb, t = rl + (rm0 << 32),
           c = t < rl;
  lo = t + (rm1 << 32);
  c += lo < t;
  hi = rh + (rm0 >> 32) + (rm1 >> 32) + c;
  *A = lo;
  *B = hi;
#endif
}

uint64_t _wymix(uint64_t A, uint64_t B) {
  _wymum(&A, &B);
  return A ^ B;
}

uint64_t _wyr4(const uint8_t* p) {
  uint32_t v;
  memcpy(&v, p, 4);
  return v;
}

uint64_t _wyr8(const uint8_t* p) {
  uint64_t v;
  memcpy(&v, p, 8);
  return v;
}

uint64_t _wyr3(const uint8_t* p, size_t k) {
  return (((uint64_t)p[0]) << 16) | (((uint64_t)p[k >> 1]) << 8) | p[k - 1];
}

uint64_t wyhash(const void* key, size_t len, uint64_t seed, const uint64_t* secret) {
  const uint8_t* p = (const uint8_t*)key;
  seed ^= _wymix(seed ^ secret[0], secret[1]);
  uint64_t a, b;
  if (_likely_(len <= 16)) {
    if (_likely_(len >= 4)) {
      a = (_wyr4(p) << 32) | _wyr4(p + ((len >> 3) << 2));
      b = (_wyr4(p + len - 4) << 32) | _wyr4(p + len - 4 - ((len >> 3) << 2));
    } else if (_likely_(len > 0)) {
      a = _wyr3(p, len);
      b = 0;
    } else
      a = b = 0;
  } else {
    size_t i = len;
    if (_unlikely_(i > 48)) {
      uint64_t see1 = seed, see2 = seed;
      do {
        seed = _wymix(_wyr8(p) ^ secret[1], _wyr8(p + 8) ^ seed);
        see1 = _wymix(_wyr8(p + 16) ^ secret[2], _wyr8(p + 24) ^ see1);
        see2 = _wymix(_wyr8(p + 32) ^ secret[3], _wyr8(p + 40) ^ see2);
        p += 48;
        i -= 48;
      } while (_likely_(i > 48));
      seed ^= see1 ^ see2;
    }
    while (_unlikely_(i > 16)) {
      seed = _wymix(_wyr8(p) ^ secret[1], _wyr8(p + 8) ^ seed);
      i -= 16;
      p += 16;
    }
    a = _wyr8(p + i - 16);
    b = _wyr8(p + i - 8);
  }
  a ^= secret[1];
  b ^= seed;
  _wymum(&a, &b);
  return _wymix(a ^ secret[0] ^ len, b ^ secret[1]);
}

// the default secret parameters
static const uint64_t _wyp[4] = {
  0xa0761d6478bd642full, 0xe7037ed1a0b428dbull, 0x8ebc6af09c88c6e3ull, 0x589965cc75374cc3ull};

uint32_t Hash32(const void *data, size_t bytes, uint32_t seed = 0) {
  return static_cast<uint32_t>(wyhash(data, bytes, seed, _wyp));
}

uint32_t Mix(uint32_t hash) {
  hash ^= hash >> 16;
  hash *= 0x85ebca6b;
  hash ^= hash >> 13;
  hash *= 0xc2b2ae35;
  hash ^= hash >> 16;
  return hash;
}


// GoodHash should usually be your first choice in hashing data.
// It should be both reasonably fast and high quality.
struct GoodHash {
  template <typename K>
  std::enable_if_t<std::has_unique_object_representations<K>::value && sizeof(K) == 4, uint32_t>
  operator()(const K& k) const {
    return Mix(*(const uint32_t*)&k);
  }

  template <typename K>
  std::enable_if_t<std::has_unique_object_representations<K>::value && sizeof(K) != 4, uint32_t>
  operator()(const K& k) const {
    return Hash32(&k, sizeof(K));
  }

  uint32_t operator()(const std::string& k) const {
    return Hash32(k.c_str(), k.size());
  }

  uint32_t operator()(std::string_view k) const {
    return Hash32(k.data(), k.size());
  }
};


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

FTScalerContext::FTScalerContext(std::shared_ptr<Typeface> tf, float size)
    : ScalerContext(std::move(tf), size), textScale(size) {
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
    err = FT_Set_Char_Size(face, textScaleDot6, textScaleDot6, 72, 72);
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
  auto* os2 = static_cast<TT_OS2*>(FT_Get_Sfnt_Table(face, FT_SFNT_OS2));
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

void FTScalerContext::getBBoxForCurrentGlyph(FT_BBox* bbox) const {
  auto face = ftTypeface()->face;
  FT_Outline_Get_CBox(&face->glyph->outline, bbox);

  // outset the box to integral boundaries
  bbox->xMin &= ~63;
  bbox->yMin &= ~63;
  bbox->xMax = (bbox->xMax + 63) & ~63;
  bbox->yMax = (bbox->yMax + 63) & ~63;
}

bool generateFacePathCOLRv1(FT_Face face, GlyphID glyphID, Path* path) {
  uint32_t flags = 0;
  flags |= FT_LOAD_BITMAP_METRICS_ONLY;  // Don't decode any bitmaps.
  flags |= FT_LOAD_NO_BITMAP;            // Ignore embedded bitmaps.
  flags &= ~FT_LOAD_RENDER;              // Don't scan convert.
  flags &= ~FT_LOAD_COLOR;               // Ignore SVG.
  flags |= FT_LOAD_NO_HINTING;
  flags |= FT_LOAD_NO_AUTOHINT;
  flags |= FT_LOAD_IGNORE_TRANSFORM;

  FT_Size size;
  FT_Error err = FT_New_Size(face, &size);
  if (err != 0) {
    LOGE("FT_New_Size(%s) failed in generateFacePathStaticCOLRv1.", face->family_name);
    return false;
  }

  FT_Size oldSize = face->size;

  auto tryGeneratePath = [face, &size, glyphID, flags, path]() {
    FT_Error err = 0;

    err = FT_Activate_Size(size);
    if (err != 0) {
      return false;
    }

    err = FT_Set_Char_Size(face, face->units_per_EM << 6, face->units_per_EM << 6, 72, 72);
    if (err != 0) {
      return false;
    }

    err = FT_Load_Glyph(face, glyphID, static_cast<FT_Int32>(flags));
    if (err != 0) {
      path->reset();
      return false;
    }

    if (!GenerateGlyphPath(face, path)) {
      path->reset();
      return false;
    }

    return true;
  };

  bool pathGenerationResult = tryGeneratePath();

  FT_Activate_Size(oldSize);

  return pathGenerationResult;
}

/* In drawing mode, concatenates the transforms directly on SkCanvas. In
 * bounding box calculation mode, no SkCanvas is specified, but we only want to
 * retrieve the transform from the FreeType paint object. */
void colrv1_transform(const FT_COLR_Paint& colrPaint, Canvas* canvas,
                      Matrix* outTransform = nullptr) {
  if (!canvas && !outTransform) {
    return;
  }
  Matrix transform;

  switch (colrPaint.format) {
    case FT_COLR_PAINTFORMAT_TRANSFORM: {
      auto affine = colrPaint.u.transform.affine;
      transform = Matrix::MakeAll(FTFixedToFloat(affine.xx), -FTFixedToFloat(affine.xy),
                                  FTFixedToFloat(affine.dx), -FTFixedToFloat(affine.yx),
                                  FTFixedToFloat(affine.yy), -FTFixedToFloat(affine.dy));
      break;
    }
    case FT_COLR_PAINTFORMAT_TRANSLATE: {
      transform = Matrix::MakeTrans(FTFixedToFloat(colrPaint.u.translate.dx),
                                    -FTFixedToFloat(colrPaint.u.translate.dy));
      break;
    }
    case FT_COLR_PAINTFORMAT_SCALE: {
      transform.setScale(
          FTFixedToFloat(colrPaint.u.scale.scale_x), FTFixedToFloat(colrPaint.u.scale.scale_y),
          FTFixedToFloat(colrPaint.u.scale.center_x), -FTFixedToFloat(colrPaint.u.scale.center_y));
      break;
    }
    case FT_COLR_PAINTFORMAT_ROTATE: {
      // COLRv1 angles are counter-clockwise, compare
      // https://docs.microsoft.com/en-us/typography/opentype/spec/colr#formats-24-to-27-paintrotate-paintvarrotate-paintrotatearoundcenter-paintvarrotatearoundcenter
      transform = Matrix::MakeRotate(-FTFixedToFloat(colrPaint.u.rotate.angle) * 180.0f,
                                     FTFixedToFloat(colrPaint.u.rotate.center_x),
                                     -FTFixedToFloat(colrPaint.u.rotate.center_y));
      break;
    }
    case FT_COLR_PAINTFORMAT_SKEW: {
      // In the PAINTFORMAT_ROTATE implementation, SkMatrix setRotate
      // snaps to 0 for values very close to 0. Do the same here.

      float xDeg = FTFixedToFloat(colrPaint.u.skew.x_skew_angle) * 180.0f;
      float xRad = DegreesToRadians(xDeg);
      float xTan = std::tan(xRad);
      xTan = FloatNearlyZero(xTan) ? 0.0f : xTan;

      float yDeg = FTFixedToFloat(colrPaint.u.skew.y_skew_angle) * 180.0f;
      // Negate y_skew_angle due to Skia's y-down coordinate system to achieve
      // counter-clockwise skew along the y-axis.
      float yRad = DegreesToRadians(-yDeg);
      float yTan = std::tan(yRad);
      yTan = FloatNearlyZero(yTan) ? 0.0f : yTan;

      transform.setSkew(xTan, yTan, FTFixedToFloat(colrPaint.u.skew.center_x),
                        -FTFixedToFloat(colrPaint.u.skew.center_y));
      break;
    }
    default:
      return;
  }
  if (canvas) {
    canvas->concat(transform);
  }
  if (outTransform) {
    *outTransform = transform;
  }
}

bool colrv1_start_glyph_bounds(Matrix *ctm,
                               Rect* bounds,
                               FT_Face face,
                               uint16_t glyphId,
                               FT_Color_Root_Transform rootTransform);

bool colrv1_traverse_paint_bounds(Matrix* ctm, Rect* bounds, FT_Face face,
                                  FT_OpaquePaint opaquePaint) {
  // Cycle detection, see section "5.7.11.1.9 Color glyphs as a directed acyclic graph".
  // if (activePaints->contains(opaquePaint)) {
  //   return false;
  // }

  // activePaints->add(opaquePaint);
  // SK_AT_SCOPE_EXIT(activePaints->remove(opaquePaint));

  FT_COLR_Paint paint;
  if (!FT_Get_Paint(face, opaquePaint, &paint)) {
    return false;
  }

//  Matrix restoreMatrix = *ctm;

  switch (paint.format) {
    case FT_COLR_PAINTFORMAT_COLR_LAYERS: {
      FT_LayerIterator& layerIterator = paint.u.colr_layers.layer_iterator;
      FT_OpaquePaint layerPaint{nullptr, 1};
      while (FT_Get_Paint_Layers(face, &layerIterator, &layerPaint)) {
        if (!colrv1_traverse_paint_bounds(ctm, bounds, face, layerPaint)) {
          return false;
        }
      }
      return true;
    }
    case FT_COLR_PAINTFORMAT_GLYPH: {
      FT_UInt glyphID = paint.u.glyph.glyphID;
      Path path;
      if (!generateFacePathCOLRv1(face, static_cast<GlyphID>(glyphID), &path)) {
        return false;
      }
      path.transform(*ctm);
      bounds->join(path.getBounds());
      return true;
    }
    case FT_COLR_PAINTFORMAT_COLR_GLYPH: {
      FT_UInt glyphID = paint.u.colr_glyph.glyphID;
      return colrv1_start_glyph_bounds(ctm, bounds, face, static_cast<uint16_t>(glyphID), FT_COLOR_NO_ROOT_TRANSFORM);
    }
    case FT_COLR_PAINTFORMAT_TRANSFORM: {
      Matrix transformMatrix;
      colrv1_transform(paint, nullptr, &transformMatrix);
      ctm->preConcat(transformMatrix);
      FT_OpaquePaint& transformPaint = paint.u.transform.paint;
      return colrv1_traverse_paint_bounds(ctm, bounds, face, transformPaint);
    }
    case FT_COLR_PAINTFORMAT_TRANSLATE: {
      Matrix transformMatrix;
      colrv1_transform(paint, nullptr, &transformMatrix);
      ctm->preConcat(transformMatrix);
      FT_OpaquePaint& translatePaint = paint.u.translate.paint;
      return colrv1_traverse_paint_bounds(ctm, bounds, face, translatePaint);
    }
    case FT_COLR_PAINTFORMAT_SCALE: {
      Matrix transformMatrix;
      colrv1_transform(paint, nullptr, &transformMatrix);
      ctm->preConcat(transformMatrix);
      FT_OpaquePaint& scalePaint = paint.u.scale.paint;
      return colrv1_traverse_paint_bounds(ctm, bounds, face, scalePaint);
    }
    case FT_COLR_PAINTFORMAT_ROTATE: {
      Matrix transformMatrix;
      colrv1_transform(paint, nullptr, &transformMatrix);
      ctm->preConcat(transformMatrix);
      FT_OpaquePaint& rotatePaint = paint.u.rotate.paint;
      return colrv1_traverse_paint_bounds(ctm, bounds, face, rotatePaint);
    }
    case FT_COLR_PAINTFORMAT_SKEW: {
      Matrix transformMatrix;
      colrv1_transform(paint, nullptr, &transformMatrix);
      ctm->preConcat(transformMatrix);
      FT_OpaquePaint& skewPaint = paint.u.skew.paint;
      return colrv1_traverse_paint_bounds(ctm, bounds, face, skewPaint);
    }
    case FT_COLR_PAINTFORMAT_COMPOSITE: {
      FT_OpaquePaint& backdropPaint = paint.u.composite.backdrop_paint;
      FT_OpaquePaint& sourcePaint = paint.u.composite.source_paint;
      return colrv1_traverse_paint_bounds(ctm, bounds, face, backdropPaint) &&
             colrv1_traverse_paint_bounds(ctm, bounds, face, sourcePaint);
    }
    case FT_COLR_PAINTFORMAT_SOLID:
    case FT_COLR_PAINTFORMAT_LINEAR_GRADIENT:
    case FT_COLR_PAINTFORMAT_RADIAL_GRADIENT:
    case FT_COLR_PAINTFORMAT_SWEEP_GRADIENT: {
      return true;
    }
    default:
      return false;
  }
  return false;
}

bool colrv1_start_glyph_bounds(Matrix *ctm,
                               Rect* bounds,
                               FT_Face face,
                               uint16_t glyphId,
                               FT_Color_Root_Transform rootTransform) {
  FT_OpaquePaint opaquePaint{nullptr, 1};
  return FT_Get_Color_Glyph_Paint(face, glyphId, rootTransform, &opaquePaint) &&
         colrv1_traverse_paint_bounds(ctm, bounds, face, opaquePaint);
}

bool computeColrV1GlyphBoundingBox(FT_Face face, GlyphID glyphId, Rect* bounds) {
  Matrix ctm;
  *bounds = Rect::MakeEmpty();
  return colrv1_start_glyph_bounds(&ctm, bounds, face, glyphId, FT_COLOR_NO_ROOT_TRANSFORM);
}

bool getBoundsOfCurrentOutlineGlyph(FT_GlyphSlot glyph, Rect* bounds) {
  if (glyph->format != FT_GLYPH_FORMAT_OUTLINE) {
    return false;
  }
  if (0 == glyph->outline.n_contours) {
    return false;
  }

  FT_BBox bbox;
  FT_Outline_Get_CBox(&glyph->outline, &bbox);
  *bounds = Rect::MakeLTRB(FDot6ToFloat(bbox.xMin), -FDot6ToFloat(bbox.yMax),
                           FDot6ToFloat(bbox.xMax), -FDot6ToFloat(bbox.yMin));
  return true;
}

Rect FTScalerContext::getBounds(tgfx::GlyphID glyphID, bool fauxBold, bool fauxItalic) const {
  std::lock_guard<std::mutex> autoLock(ftTypeface()->locker);
  auto bounds = Rect::MakeEmpty();
  if (setupSize(fauxItalic)) {
    return bounds;
  }
  bool haveLayers = false;
  auto face = ftTypeface()->face;
  #ifdef  FT_COLOR_H
    // See https://skbug.com/12945, if the face isn't marked scalable then paths cannot be loaded.
    if (FT_IS_SCALABLE(face)) {
  #ifdef TT_SUPPORT_COLRV1
      FT_OpaquePaint opaqueLayerPaint{nullptr, 1};
          if (FT_Get_Color_Glyph_Paint(face, glyphID,
                                       FT_COLOR_INCLUDE_ROOT_TRANSFORM, &opaqueLayerPaint)) {
              haveLayers = true;

              // COLRv1 optionally provides a ClipBox.
              FT_ClipBox clipBox;
              if (FT_Get_Color_Glyph_ClipBox(face, glyphID, &clipBox)) {
                  // Find bounding box of clip box corner points, needed when clipbox is transformed.
                  FT_BBox bbox;
                  bbox.xMin = clipBox.bottom_left.x;
                  bbox.xMax = clipBox.bottom_left.x;
                  bbox.yMin = clipBox.bottom_left.y;
                  bbox.yMax = clipBox.bottom_left.y;
                  for (auto& corner : {clipBox.top_left, clipBox.top_right, clipBox.bottom_right}) {
                      bbox.xMin = std::min(bbox.xMin, corner.x);
                      bbox.yMin = std::min(bbox.yMin, corner.y);
                      bbox.xMax = std::max(bbox.xMax, corner.x);
                      bbox.yMax = std::max(bbox.yMax, corner.y);
                  }
                  bounds = Rect::MakeLTRB(FDot6ToFloat(bbox.xMin), -FDot6ToFloat(bbox.yMax),
                                            FDot6ToFloat(bbox.xMax), -FDot6ToFloat(bbox.yMin));
              } else {
                  // Traverse the glyph graph with a focus on measuring the required bounding box.
                  // The call to computeColrV1GlyphBoundingBox may modify the face.
                  // Reset the face to load the base glyph for metrics.
                  if (!computeColrV1GlyphBoundingBox(face, glyphID, &bounds) ||
                      setupSize(fauxItalic))
                  {
                      return bounds;
                  }
              }
          }
  #endif
      if (!haveLayers) {
        FT_LayerIterator layerIterator = { 0, 0, nullptr };
        FT_UInt layerGlyphIndex;
        FT_UInt layerColorIndex;
        FT_Int32 flags = loadGlyphFlags;
        flags |= FT_LOAD_BITMAP_METRICS_ONLY;  // Don't decode any bitmaps.
        flags |= FT_LOAD_NO_BITMAP; // Ignore embedded bitmaps.
        flags &= ~FT_LOAD_RENDER;  // Don't scan convert.
        flags &= ~FT_LOAD_COLOR;  // Ignore SVG.
        // For COLRv0 compute the glyph bounding box from the union of layer bounding boxes.
        while (FT_Get_Color_Glyph_Layer(face, glyphID, &layerGlyphIndex,
                                        &layerColorIndex, &layerIterator)) {
          haveLayers = true;
          if (FT_Load_Glyph(face, layerGlyphIndex, flags)) {
            return bounds;
          }

          Rect currentBounds;
          if (getBoundsOfCurrentOutlineGlyph(face->glyph, &currentBounds)) {
            bounds.join(currentBounds);
          }
                                        }
//        if (haveLayers) {
//          mx.extraBits = ScalerContextBits::COLRv0;
//        }
      }

      if (haveLayers) {
//        mx.maskFormat = SkMask::kARGB32_Format;
//        mx.neverRequestPath = true;
//        updateGlyphBoundsIfSubpixel(glyph, &bounds, this->isSubpixel());
//        mx.bounds = bounds;
      }
    }
  #endif

  auto glyphFlags = loadGlyphFlags | static_cast<FT_Int32>(FT_LOAD_BITMAP_METRICS_ONLY);
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

Rect FTScalerContext::getImageTransform(GlyphID glyphID, Matrix* matrix) const {
  std::lock_guard<std::mutex> autoLock(ftTypeface()->locker);
  auto glyphFlags = loadGlyphFlags | static_cast<FT_Int32>(FT_LOAD_BITMAP_METRICS_ONLY);
  glyphFlags &= ~FT_LOAD_NO_BITMAP;
  if (!loadBitmapGlyph(glyphID, glyphFlags)) {
    return Rect::MakeEmpty();
  }
  auto face = ftTypeface()->face;
  if (matrix) {
    matrix->setTranslate(static_cast<float>(face->glyph->bitmap_left),
                         -static_cast<float>(face->glyph->bitmap_top));
    matrix->postScale(extraScale.x, extraScale.y);
  }
  return Rect::MakeXYWH(
      static_cast<float>(face->glyph->bitmap_left), -static_cast<float>(face->glyph->bitmap_top),
      static_cast<float>(face->glyph->bitmap.width), static_cast<float>(face->glyph->bitmap.rows));
}

std::shared_ptr<ImageBuffer> FTScalerContext::generateImage(GlyphID glyphID,
                                                            bool tryHardware) const {
  std::lock_guard<std::mutex> autoLock(ftTypeface()->locker);
  auto glyphFlags = loadGlyphFlags;
  glyphFlags |= FT_LOAD_RENDER;
  glyphFlags &= ~FT_LOAD_NO_BITMAP;
  if (!loadBitmapGlyph(glyphID, glyphFlags)) {
    return nullptr;
  }
  auto ftBitmap = ftTypeface()->face->glyph->bitmap;
  auto alphaOnly = ftBitmap.pixel_mode == FT_PIXEL_MODE_GRAY;
  Bitmap bitmap(static_cast<int>(ftBitmap.width), static_cast<int>(ftBitmap.rows), alphaOnly,
                tryHardware);
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
}  // namespace tgfx
