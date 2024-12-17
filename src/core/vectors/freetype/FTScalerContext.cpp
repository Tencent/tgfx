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
#include <tgfx/core/Canvas.h>
#include <tgfx/core/Recorder.h>
#include <tgfx/core/Surface.h>
#include <tgfx/gpu/opengl/GLDevice.h>
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
const uint16_t kForegroundColorPaletteIndex = 0xFFFF;

static float FTFixedToFloat(FT_Fixed x) {
  return static_cast<float>(x) * 1.52587890625e-5f;
}

constexpr float kColorStopShift =
    sizeof(FT_ColorStop::stop_offset) == sizeof(FT_F2Dot14) ? 1 << 14 : 1 << 16;

static FT_Fixed FloatToFTFixed(float x) {
  static constexpr float MaxS32FitsInFloat = 2147483520.f;
  static constexpr float MinS32FitsInFloat = -MaxS32FitsInFloat;
  x = x < MaxS32FitsInFloat ? x : MaxS32FitsInFloat;
  x = x > MinS32FitsInFloat ? x : MinS32FitsInFloat;
  return static_cast<FT_Fixed>(x * (1 << 16));
}

enum class SkTileMode {
  /**
   *  Replicate the edge color if the shader draws outside of its
   *  original bounds.
   */
  kClamp,

  /**
   *  Repeat the shader's image horizontally and vertically.
   */
  kRepeat,

  /**
   *  Repeat the shader's image horizontally and vertically, alternating
   *  mirror images so that adjacent images always seam.
   */
  kMirror,

  /**
   *  Only draw within the original domain, return transparent-black everywhere else.
   */
  kDecal,

  kLastTileMode = kDecal,
};

SkTileMode ToSkTileMode(FT_PaintExtend extendMode) {
  switch (extendMode) {
    case FT_COLR_PAINT_EXTEND_REPEAT:
      return SkTileMode::kRepeat;
    case FT_COLR_PAINT_EXTEND_REFLECT:
      return SkTileMode::kMirror;
    default:
      return SkTileMode::kClamp;
  }
}

enum TruncateStops { TruncateStart, TruncateEnd };

uint32_t InterpolateColor(uint32_t color1, uint32_t color2, float t) {
  if (t < 0) {
    return color1;
  }
  if (t > 1) {
    return color2;
  }
  // 分离 RGBA 分量
  uint32_t r1 = (color1 >> 24) & 0xFF;
  uint32_t g1 = (color1 >> 16) & 0xFF;
  uint32_t b1 = (color1 >> 8) & 0xFF;
  uint32_t a1 = color1 & 0xFF;

  uint32_t r2 = (color2 >> 24) & 0xFF;
  uint32_t g2 = (color2 >> 16) & 0xFF;
  uint32_t b2 = (color2 >> 8) & 0xFF;
  uint32_t a2 = color2 & 0xFF;

  // 线性插值计算
  uint32_t r = static_cast<uint32_t>((1 - t) * r1 + t * r2);
  uint32_t g = static_cast<uint32_t>((1 - t) * g1 + t * g2);
  uint32_t b = static_cast<uint32_t>((1 - t) * b1 + t * b2);
  uint32_t a = static_cast<uint32_t>((1 - t) * a1 + t * a2);

  // 合并 RGBA 分量
  return (r << 24) | (g << 16) | (b << 8) | a;
}


std::shared_ptr<ScalerContext> ScalerContext::CreateNew(std::shared_ptr<Typeface> typeface,
                                                        float size) {
  DEBUG_ASSERT(typeface != nullptr);
  return std::make_shared<FTScalerContext>(std::move(typeface), size);
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

static bool GenerateGlyphPath(FT_Face face, Path* path);
bool colrv1_traverse_paint_bounds(Matrix* ctm, Rect* bounds, FT_Face face,
                                  FT_OpaquePaint opaquePaint);

bool colrv1_start_glyph_bounds(Matrix* ctm, Rect* bounds, FT_Face face, uint16_t glyphId,
                               FT_Color_Root_Transform rootTransform) {
  FT_OpaquePaint opaquePaint{nullptr, 1};
  return FT_Get_Color_Glyph_Paint(face, glyphId, rootTransform, &opaquePaint) &&
         colrv1_traverse_paint_bounds(ctm, bounds, face, opaquePaint);
}

bool generateFacePathCOLRv1(FT_Face face, GlyphID glyphID, Path* path) {
  FT_Int32 flags = 0;
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

    err = FT_Load_Glyph(face, glyphID, flags);
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
void colrv1_transform(FT_Face, const FT_COLR_Paint& colrPaint, Canvas* canvas,
                      Matrix* outTransform = nullptr) {
  Matrix transform;

  // SkASSERT(canvas || outTransform);

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
    default: {
      break;
    }
  }
  if (canvas) {
    canvas->concat(transform);
  }
  if (outTransform) {
    *outTransform = transform;
  }
}

BlendMode ToBlendMode(FT_Composite_Mode compositeMode) {
  switch (compositeMode) {
    case FT_COLR_COMPOSITE_CLEAR:
      return BlendMode::Clear;
    case FT_COLR_COMPOSITE_SRC:
      return BlendMode::Src;
    case FT_COLR_COMPOSITE_DEST:
      return BlendMode::Dst;
    case FT_COLR_COMPOSITE_SRC_OVER:
      return BlendMode::SrcOver;
    case FT_COLR_COMPOSITE_DEST_OVER:
      return BlendMode::DstOver;
    case FT_COLR_COMPOSITE_SRC_IN:
      return BlendMode::SrcIn;
    case FT_COLR_COMPOSITE_DEST_IN:
      return BlendMode::DstIn;
    case FT_COLR_COMPOSITE_SRC_OUT:
      return BlendMode::SrcOut;
    case FT_COLR_COMPOSITE_DEST_OUT:
      return BlendMode::DstOut;
    case FT_COLR_COMPOSITE_SRC_ATOP:
      return BlendMode::SrcATop;
    case FT_COLR_COMPOSITE_DEST_ATOP:
      return BlendMode::DstATop;
    case FT_COLR_COMPOSITE_XOR:
      return BlendMode::Xor;
    // case FT_COLR_COMPOSITE_PLUS:
    //     return BlendMode::Plus;
    case FT_COLR_COMPOSITE_SCREEN:
      return BlendMode::Screen;
    case FT_COLR_COMPOSITE_OVERLAY:
      return BlendMode::Overlay;
    case FT_COLR_COMPOSITE_DARKEN:
      return BlendMode::Darken;
    case FT_COLR_COMPOSITE_LIGHTEN:
      return BlendMode::Lighten;
    case FT_COLR_COMPOSITE_COLOR_DODGE:
      return BlendMode::ColorDodge;
    case FT_COLR_COMPOSITE_COLOR_BURN:
      return BlendMode::ColorBurn;
    case FT_COLR_COMPOSITE_HARD_LIGHT:
      return BlendMode::HardLight;
    case FT_COLR_COMPOSITE_SOFT_LIGHT:
      return BlendMode::SoftLight;
    case FT_COLR_COMPOSITE_DIFFERENCE:
      return BlendMode::Difference;
    case FT_COLR_COMPOSITE_EXCLUSION:
      return BlendMode::Exclusion;
    case FT_COLR_COMPOSITE_MULTIPLY:
      return BlendMode::Multiply;
    case FT_COLR_COMPOSITE_HSL_HUE:
      return BlendMode::Hue;
    case FT_COLR_COMPOSITE_HSL_SATURATION:
      return BlendMode::Saturation;
    case FT_COLR_COMPOSITE_HSL_COLOR:
      return BlendMode::Color;
    case FT_COLR_COMPOSITE_HSL_LUMINOSITY:
      return BlendMode::Luminosity;
    default:
      return BlendMode::Dst;
  }
}

bool colrv1_traverse_paint_bounds(Matrix* ctm, Rect* bounds, FT_Face face,
                                  FT_OpaquePaint opaquePaint) {
  FT_COLR_Paint paint;
  if (!FT_Get_Paint(face, opaquePaint, &paint)) {
    return false;
  }

  // Matrix restoreMatrix = *ctm;

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
      GlyphID glyphID = static_cast<GlyphID>(paint.u.glyph.glyphID);
      Path path;
      if (!generateFacePathCOLRv1(face, glyphID, &path)) {
        return false;
      }
      path.transform(*ctm);
      bounds->join(path.getBounds());
      return true;
    }
    case FT_COLR_PAINTFORMAT_COLR_GLYPH: {
      GlyphID glyphID = static_cast<GlyphID>(paint.u.colr_glyph.glyphID);
      return colrv1_start_glyph_bounds(ctm, bounds, face, glyphID, FT_COLOR_NO_ROOT_TRANSFORM);
    }
    case FT_COLR_PAINTFORMAT_TRANSFORM: {
      Matrix transformMatrix;
      colrv1_transform(face, paint, nullptr, &transformMatrix);
      ctm->preConcat(transformMatrix);
      FT_OpaquePaint& transformPaint = paint.u.transform.paint;
      return colrv1_traverse_paint_bounds(ctm, bounds, face, transformPaint);
    }
    case FT_COLR_PAINTFORMAT_TRANSLATE: {
      Matrix transformMatrix;
      colrv1_transform(face, paint, nullptr, &transformMatrix);
      ctm->preConcat(transformMatrix);
      FT_OpaquePaint& translatePaint = paint.u.translate.paint;
      return colrv1_traverse_paint_bounds(ctm, bounds, face, translatePaint);
    }
    case FT_COLR_PAINTFORMAT_SCALE: {
      Matrix transformMatrix;
      colrv1_transform(face, paint, nullptr, &transformMatrix);
      ctm->preConcat(transformMatrix);
      FT_OpaquePaint& scalePaint = paint.u.scale.paint;
      return colrv1_traverse_paint_bounds(ctm, bounds, face, scalePaint);
    }
    case FT_COLR_PAINTFORMAT_ROTATE: {
      Matrix transformMatrix;
      colrv1_transform(face, paint, nullptr, &transformMatrix);
      ctm->preConcat(transformMatrix);
      FT_OpaquePaint& rotatePaint = paint.u.rotate.paint;
      return colrv1_traverse_paint_bounds(ctm, bounds, face, rotatePaint);
    }
    case FT_COLR_PAINTFORMAT_SKEW: {
      Matrix transformMatrix;
      colrv1_transform(face, paint, nullptr, &transformMatrix);
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
}

uint32_t multiplyAlpha(uint32_t color, float alphaMultiplier) {
  uint32_t r = (color >> 16) & 0xFF;
  uint32_t g = (color >> 8) & 0xFF;
  uint32_t b = color & 0xFF;
  uint32_t a = (color >> 24) & 0xFF;

  // 计算新的 alpha 值
  uint32_t newAlpha = static_cast<uint32_t>(std::clamp(a * alphaMultiplier, 0.0f, 255.0f));

  // 重新组合成 uint32_t 类型的颜色值
  return (newAlpha << 24) | (r << 16) | (g << 8) | b;
}

Color Uint32ToColor(uint32_t color) {
  float r = ((color >> 16) & 0xFF) / 255.0f;
  float g = ((color >> 8) & 0xFF) / 255.0f;
  float b = (color & 0xFF) / 255.0f;
  float a = ((color >> 24) & 0xFF) / 255.0f;
  return Color{r, g, b, a};
}

bool computeColrV1GlyphBoundingBox(FT_Face face, GlyphID glyphID, Rect* bounds) {
  Matrix ctm;
  *bounds = Rect::MakeEmpty();
  return colrv1_start_glyph_bounds(&ctm, bounds, face, glyphID, FT_COLOR_INCLUDE_ROOT_TRANSFORM);
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

// Truncate a vector of color stops at a previously computed stop position and insert at that
// position the color interpolated between the surrounding stops.
void truncateToStopInterpolating(float zeroRadiusStop, std::vector<uint32_t>& colors,
                                 std::vector<float>& stops, TruncateStops truncateStops) {
  if (stops.size() <= 1u || zeroRadiusStop < stops.front() || stops.back() < zeroRadiusStop) {
    return;
  }

  size_t afterIndex =
      static_cast<size_t>((truncateStops == TruncateStart)
          ? std::lower_bound(stops.begin(), stops.end(), zeroRadiusStop) - stops.begin()
          : std::upper_bound(stops.begin(), stops.end(), zeroRadiusStop) - stops.begin());

  const float t =
      (zeroRadiusStop - stops[afterIndex - 1]) / (stops[afterIndex] - stops[afterIndex - 1]);
  uint32_t lerpColor = InterpolateColor(colors[afterIndex - 1], colors[afterIndex], t);

  if (truncateStops == TruncateStart) {
    stops.erase(stops.begin(), stops.begin() + static_cast<long>(afterIndex));
    colors.erase(colors.begin(), colors.begin() + static_cast<long>(afterIndex));
    stops.insert(stops.begin(), 0);
    colors.insert(colors.begin(), lerpColor);
  } else {
    stops.erase(stops.begin() + static_cast<long>(afterIndex), stops.end());
    colors.erase(colors.begin() + static_cast<long>(afterIndex), colors.end());
    stops.insert(stops.end(), 1);
    colors.insert(colors.end(), lerpColor);
  }
}

Point SkVectorProjection(Point a, Point b) {
  float length = b.length();
  if (length <= 0) {
    return Point::Zero();
  }
  Point bNormalized = b;
  bNormalized.normalize();
  bNormalized *= (Point::DotProduct(a, b) / length);
  return bNormalized;
}

Path GetClipBoxPath(FT_Face face, uint16_t glyphId, bool untransformed) {
  Path resultPath;
  FT_Size oldSize = face->size;
  FT_Matrix oldTransform;
  FT_Vector oldDelta;
  FT_Error err = 0;

  FT_Size size;
  if (untransformed) {
    FT_Error err = FT_New_Size(face, &size);
    if (err != 0) {
      LOGE("FT_New_Size(%s) failed in generateFacePathStaticCOLRv1.", face->family_name);
      return resultPath;
    }
    err = FT_Activate_Size(size);
    if (err != 0) {
      return resultPath;
    }

    err = FT_Set_Char_Size(face, face->units_per_EM << 6, 0, 0, 0);
    if (err != 0) {
      return resultPath;
    }

    FT_Get_Transform(face, &oldTransform, &oldDelta);
    FT_Set_Transform(face, nullptr, nullptr);
  }

  FT_ClipBox colrGlyphClipBox;
  if (FT_Get_Color_Glyph_ClipBox(face, glyphId, &colrGlyphClipBox)) {
    resultPath.moveTo(FTFixedToFloat(colrGlyphClipBox.bottom_left.x),
                      -FTFixedToFloat(colrGlyphClipBox.bottom_left.y));
    resultPath.lineTo(FTFixedToFloat(colrGlyphClipBox.top_left.x),
                      -FTFixedToFloat(colrGlyphClipBox.top_left.y));
    resultPath.lineTo(FTFixedToFloat(colrGlyphClipBox.top_right.x),
                      -FTFixedToFloat(colrGlyphClipBox.top_right.y));
    resultPath.lineTo(FTFixedToFloat(colrGlyphClipBox.bottom_right.x),
                      -FTFixedToFloat(colrGlyphClipBox.bottom_right.y));
    resultPath.close();
  }

  if (untransformed) {
    err = FT_Activate_Size(oldSize);
    if (err != 0) {
      return resultPath;
    }
    FT_Set_Transform(face, &oldTransform, &oldDelta);
  }

  return resultPath;
}

bool colrv1_traverse_paint(Canvas* canvas, const std::vector<uint32_t>& palette,
                           const uint32_t foregroundColor, FT_Face face,
                           FT_OpaquePaint opaquePaint);

bool colrv1_start_glyph(Canvas* canvas, const std::vector<uint32_t>& palette,
                        const uint32_t foregroundColor, FT_Face face, uint16_t glyphId,
                        FT_Color_Root_Transform rootTransform) {
  FT_OpaquePaint opaquePaint{nullptr, 1};
  if (!FT_Get_Color_Glyph_Paint(face, glyphId, rootTransform, &opaquePaint)) {
    return false;
  }

  bool untransformed = rootTransform == FT_COLOR_NO_ROOT_TRANSFORM;
  Path clipBoxPath = GetClipBoxPath(face, glyphId, untransformed);
  if (!clipBoxPath.isEmpty()) {
    canvas->clipPath(clipBoxPath);
  }

  if (!colrv1_traverse_paint(canvas, palette, foregroundColor, face, opaquePaint)) {
    return false;
  }

  return true;
}

bool colrv1_configure_skpaint(FT_Face face, const std::vector<uint32_t>& palette,
                              const uint32_t foregroundColor, const FT_COLR_Paint& colrPaint,
                              Paint* paint) {
  auto fetchColorStops = [&face, &palette, &foregroundColor](
                             const FT_ColorStopIterator& colorStopIterator,
                             std::vector<float>& stops, std::vector<uint32_t>& colors) -> bool {
    const FT_UInt colorStopCount = colorStopIterator.num_color_stops;
    if (colorStopCount == 0) {
      return false;
    }

    // 5.7.11.2.4 ColorIndex, ColorStop and ColorLine
    // "Applications shall apply the colorStops in increasing stopOffset order."
    struct ColorStop {
      float pos;
      uint32_t color;
    };
    std::vector<ColorStop> colorStopsSorted;
    colorStopsSorted.resize(colorStopCount);

    FT_ColorStop ftStop;
    FT_ColorStopIterator mutable_color_stop_iterator = colorStopIterator;
    while (FT_Get_Colorline_Stops(face, &ftStop, &mutable_color_stop_iterator)) {
      FT_UInt index = mutable_color_stop_iterator.current_color_stop - 1;
      ColorStop& skStop = colorStopsSorted[index];
      skStop.pos = ftStop.stop_offset / kColorStopShift;
      FT_UInt16& palette_index = ftStop.color.palette_index;
      if (palette_index == kForegroundColorPaletteIndex) {
        skStop.color = foregroundColor;
      } else if (palette_index >= palette.size()) {
        return false;
      } else {
        skStop.color = palette[palette_index];
      }
      skStop.color = multiplyAlpha(skStop.color, ftStop.color.alpha / float(1 << 14));
    }

    std::stable_sort(colorStopsSorted.begin(), colorStopsSorted.end(),
                     [](const ColorStop& a, const ColorStop& b) { return a.pos < b.pos; });

    stops.resize(colorStopCount);
    colors.resize(colorStopCount);
    for (size_t i = 0; i < colorStopCount; ++i) {
      stops[i] = colorStopsSorted[i].pos;
      colors[i] = colorStopsSorted[i].color;
    }
    return true;
  };

  switch (colrPaint.format) {
    case FT_COLR_PAINTFORMAT_SOLID: {
      FT_PaintSolid solid = colrPaint.u.solid;

      // Dont' draw anything with this color if the palette index is out of bounds.
      uint32_t color = 0x00000000;
      if (solid.color.palette_index == kForegroundColorPaletteIndex) {
        color = foregroundColor;
      } else if (solid.color.palette_index >= palette.size()) {
        return false;
      } else {
        color = palette[solid.color.palette_index];
      }
      color = multiplyAlpha(color, solid.color.alpha / float(1 << 14));
      paint->setShader(nullptr);
      paint->setColor(Uint32ToColor(color));
      return true;
    }
    case FT_COLR_PAINTFORMAT_LINEAR_GRADIENT: {
      const FT_PaintLinearGradient& linearGradient = colrPaint.u.linear_gradient;
      std::vector<float> stops;
      std::vector<uint32_t> colors;

      if (!fetchColorStops(linearGradient.colorline.color_stop_iterator, stops, colors)) {
        return false;
      }

      if (stops.size() == 1) {
        paint->setColor(Uint32ToColor(colors[0]));
        return true;
      }

      Point linePositions[2] = {
          Point::Make(FTFixedToFloat(linearGradient.p0.x), -FTFixedToFloat(linearGradient.p0.y)),
          Point::Make(FTFixedToFloat(linearGradient.p1.x), -FTFixedToFloat(linearGradient.p1.y))};
      Point p0 = linePositions[0];
      Point p1 = linePositions[1];
      Point p2 =
          Point::Make(FTFixedToFloat(linearGradient.p2.x), -FTFixedToFloat(linearGradient.p2.y));

      // If p0p1 or p0p2 are degenerate probably nothing should be drawn.
      // If p0p1 and p0p2 are parallel then one side is the first color and the other side is
      // the last color, depending on the direction.
      // For now, just use the first color.
      if (p1 == p0 || p2 == p0 || Point::CrossProduct(p1 - p0, p2 - p0) <= 0) {
        paint->setColor(Uint32ToColor(colors[0]));
        return true;
      }

      // Follow implementation note in nanoemoji:
      // https://github.com/googlefonts/nanoemoji/blob/0ac6e7bb4d8202db692574d8530a9b643f1b3b3c/src/nanoemoji/svg.py#L188
      // to compute a new gradient end point P3 as the orthogonal
      // projection of the vector from p0 to p1 onto a line perpendicular
      // to line p0p2 and passing through p0.
      Point perpendicularToP2P0 = p2 - p0;
      perpendicularToP2P0 = Point::Make(perpendicularToP2P0.y, -perpendicularToP2P0.x);
      Point p3 = p0 + SkVectorProjection((p1 - p0), perpendicularToP2P0);
      linePositions[1] = p3;

      // Project/scale points according to stop extrema along p0p3 line,
      // p3 being the result of the projection above, then scale stops to
      // to [0, 1] range so that repeat modes work.  The Skia linear
      // gradient shader performs the repeat modes over the 0 to 1 range,
      // that's why we need to scale the stops to within that range.
      SkTileMode tileMode = ToSkTileMode(linearGradient.colorline.extend);
      float colorStopRange = stops.back() - stops.front();
      // If the color stops are all at the same offset position, repeat and reflect modes
      // become meaningless.
      if (colorStopRange == 0.f) {
        if (tileMode != SkTileMode::kClamp) {
          paint->setColor(Color::Transparent());
          return true;
        } else {
          // Insert duplicated fake color stop in pad case at +1.0f to enable the projection
          // of circles for an originally 0-length color stop range. Adding this stop will
          // paint the equivalent gradient, because: All font specified color stops are in the
          // same spot, mode is pad, so everything before this spot is painted with the first
          // color, everything after this spot is painted with the last color. Not adding this
          // stop will skip the projection and result in specifying non-normalized color stops
          // to the shader.
          stops.push_back(stops.back() + 1.0f);
          colors.push_back(colors.back());
          colorStopRange = 1.0f;
        }
      }
      // SkASSERT(colorStopRange != 0.f);

      // If the colorStopRange is 0 at this point, the default behavior of the shader is to
      // clamp to 1 color stops that are above 1, clamp to 0 for color stops that are below 0,
      // and repeat the outer color stops at 0 and 1 if the color stops are inside the
      // range. That will result in the correct rendering.
      if ((colorStopRange != 1 || stops.front() != 0.f)) {
        Point p0p3 = p3 - p0;
        Point p0Offset = p0p3;
        p0Offset *= stops.front();
        Point p1Offset = p0p3;
        p1Offset *= stops.back();

        linePositions[0] = p0 + p0Offset;
        linePositions[1] = p0 + p1Offset;

        float scaleFactor = 1 / colorStopRange;
        float startOffset = stops.front();
        for (float& stop : stops) {
          stop = (stop - startOffset) * scaleFactor;
        }
      }
      // sk_sp<SkShader> shader(SkGradientShader::MakeLinear(
      //     linePositions,
      //     colors.data(), SkColorSpace::MakeSRGB(), stops.data(), stops.size(),
      //     tileMode,
      //     SkGradientShader::Interpolation{
      //         SkGradientShader::Interpolation::InPremul::kNo,
      //         SkGradientShader::Interpolation::ColorSpace::kSRGB,
      //         SkGradientShader::Interpolation::HueMethod::kShorter
      //     },
      //     nullptr));

      // SkASSERT(shader);
      // An opaque color is needed to ensure the gradient is not modulated by alpha.
      paint->setColor(Color::Black());
      // paint->setShader(shader);
      return true;
    }
    case FT_COLR_PAINTFORMAT_RADIAL_GRADIENT: {
      const FT_PaintRadialGradient& radialGradient = colrPaint.u.radial_gradient;
      Point start =
          Point::Make(FTFixedToFloat(radialGradient.c0.x), -FTFixedToFloat(radialGradient.c0.y));
      float startRadius = FTFixedToFloat(radialGradient.r0);
      Point end =
          Point::Make(FTFixedToFloat(radialGradient.c1.x), -FTFixedToFloat(radialGradient.c1.y));
      float endRadius = FTFixedToFloat(radialGradient.r1);

      std::vector<float> stops;
      std::vector<uint32_t> colors;
      if (!fetchColorStops(radialGradient.colorline.color_stop_iterator, stops, colors)) {
        return false;
      }

      if (stops.size() == 1) {
        paint->setColor(Uint32ToColor(colors[0]));
        return true;
      }

      float colorStopRange = stops.back() - stops.front();
      SkTileMode tileMode = ToSkTileMode(radialGradient.colorline.extend);

      if (colorStopRange == 0.f) {
        if (tileMode != SkTileMode::kClamp) {
          paint->setColor(Color::Transparent());
          return true;
        } else {
          // Insert duplicated fake color stop in pad case at +1.0f to enable the projection
          // of circles for an originally 0-length color stop range. Adding this stop will
          // paint the equivalent gradient, because: All font specified color stops are in the
          // same spot, mode is pad, so everything before this spot is painted with the first
          // color, everything after this spot is painted with the last color. Not adding this
          // stop will skip the projection and result in specifying non-normalized color stops
          // to the shader.
          stops.push_back(stops.back() + 1.0f);
          colors.push_back(colors.back());
          colorStopRange = 1.0f;
        }
      }
      // SkASSERT(colorStopRange != 0.f);

      // If the colorStopRange is 0 at this point, the default behavior of the shader is to
      // clamp to 1 color stops that are above 1, clamp to 0 for color stops that are below 0,
      // and repeat the outer color stops at 0 and 1 if the color stops are inside the
      // range. That will result in the correct rendering.
      if (colorStopRange != 1 || stops.front() != 0.f) {
        // For the Skia two-point caonical shader to understand the
        // COLRv1 color stops we need to scale stops to 0 to 1 range and
        // interpolate new centers and radii. Otherwise the shader
        // clamps stops outside the range to 0 and 1 (larger interval)
        // or repeats the outer stops at 0 and 1 if the (smaller
        // interval).
        Point startToEnd = end - start;
        float radiusDiff = endRadius - startRadius;
        float scaleFactor = 1 / colorStopRange;
        float stopsStartOffset = stops.front();

        Point startOffset = startToEnd;
        startOffset *= stops.front();
        Point endOffset = startToEnd;
        endOffset *= stops.back();

        // The order of the following computations is important in order to avoid
        // overwriting start or startRadius before the second reassignment.
        end = start + endOffset;
        start = start + startOffset;
        endRadius = startRadius + radiusDiff * stops.back();
        startRadius = startRadius + radiusDiff * stops.front();

        for (auto& stop : stops) {
          stop = (stop - stopsStartOffset) * scaleFactor;
        }
      }

      // For negative radii, interpolation is needed to prepare parameters suitable
      // for invoking the shader. Implementation below as resolution discussed in
      // https://github.com/googlefonts/colr-gradients-spec/issues/367.
      // Truncate to manually interpolated color for tile mode clamp, otherwise
      // calculate positive projected circles.
      if (startRadius < 0 || endRadius < 0) {
        if (startRadius == endRadius && startRadius < 0) {
          paint->setColor(Color::Transparent());
          return true;
        }

        if (tileMode == SkTileMode::kClamp) {
          Point startToEnd = end - start;
          float radiusDiff = endRadius - startRadius;
          float zeroRadiusStop = 0.f;
          TruncateStops truncateSide = TruncateStart;
          if (startRadius < 0) {
            truncateSide = TruncateStart;

            // Compute color stop position where radius is = 0.  After the scaling
            // of stop positions to the normal 0,1 range that we have done above,
            // the size of the radius as a function of the color stops is: r(x) = r0
            // + x*(r1-r0) Solving this function for r(x) = 0, we get: x = -r0 /
            // (r1-r0)
            zeroRadiusStop = -startRadius / (endRadius - startRadius);
            startRadius = 0.f;
            Point startEndDiff = end - start;
            startEndDiff *= zeroRadiusStop;
            start = start + startEndDiff;
          }

          if (endRadius < 0) {
            truncateSide = TruncateEnd;
            zeroRadiusStop = -startRadius / (endRadius - startRadius);
            endRadius = 0.f;
            Point startEndDiff = end - start;
            startEndDiff *= (1 - zeroRadiusStop);
            end = end - startEndDiff;
          }

          if (!(startRadius == 0 && endRadius == 0)) {
            truncateToStopInterpolating(zeroRadiusStop, colors, stops, truncateSide);
          } else {
            // If both radii have become negative and where clamped to 0, we need to
            // produce a single color cone, otherwise the shader colors the whole
            // plane in a single color when two radii are specified as 0.
            if (radiusDiff > 0) {
              end = start + startToEnd;
              endRadius = radiusDiff;
              colors.erase(colors.begin(), colors.end() - 1);
              stops.erase(stops.begin(), stops.end() - 1);
            } else {
              start -= startToEnd;
              startRadius = -radiusDiff;
              colors.erase(colors.begin() + 1, colors.end());
              stops.erase(stops.begin() + 1, stops.end());
            }
          }
        } else {
          if (startRadius < 0 || endRadius < 0) {
            auto roundIntegerMultiple = [](float factorZeroCrossing, SkTileMode tileMode) {
              int roundedMultiple = static_cast<int>(factorZeroCrossing > 0 ? ceilf(factorZeroCrossing)
                                                           : floorf(factorZeroCrossing) - 1);
              if (tileMode == SkTileMode::kMirror && roundedMultiple % 2 != 0) {
                roundedMultiple += roundedMultiple < 0 ? -1 : 1;
              }
              return roundedMultiple;
            };

            Point startToEnd = end - start;
            float radiusDiff = endRadius - startRadius;
            float factorZeroCrossing = (startRadius / (startRadius - endRadius));
            bool inRange = 0.f <= factorZeroCrossing && factorZeroCrossing <= 1.0f;
            float direction = inRange && radiusDiff < 0 ? -1.0f : 1.0f;
            float circleProjectionFactor =
                roundIntegerMultiple(factorZeroCrossing * direction, tileMode);
            startToEnd *= circleProjectionFactor;
            startRadius += circleProjectionFactor * radiusDiff;
            endRadius += circleProjectionFactor * radiusDiff;
            start += startToEnd;
            end += startToEnd;
          }
        }
      }

      // An opaque color is needed to ensure the gradient is not modulated by alpha.
      paint->setColor(Color::Black());

      Point center = Point::Make((start.x + end.x) / 2.0f, (start.x + end.y) / 2.0f);
      std::vector<Color> colorDatas;
      for (auto color : colors) {
        colorDatas.push_back(Uint32ToColor(color));
      }
      auto shader = Shader::MakeConicGradient(center, startRadius, endRadius, colorDatas, stops);
      paint->setShader(shader);

      // paint->setShader(SkGradientShader::MakeTwoPointConical(
      //     start, startRadius, end, endRadius, colors.data(), SkColorSpace::MakeSRGB(), stops.data(),
      //     stops.size(), tileMode,
      //     SkGradientShader::Interpolation{SkGradientShader::Interpolation::InPremul::kNo,
      //                                     SkGradientShader::Interpolation::ColorSpace::kSRGB,
      //                                     SkGradientShader::Interpolation::HueMethod::kShorter},
      //     nullptr));

      return true;
    }
    case FT_COLR_PAINTFORMAT_SWEEP_GRADIENT: {
      const FT_PaintSweepGradient& sweepGradient = colrPaint.u.sweep_gradient;
      // Point center = Point::Make(FTFixedToFloat(sweepGradient.center.x),
      //                            -FTFixedToFloat(sweepGradient.center.y));

      float startAngle = FTFixedToFloat(sweepGradient.start_angle) * 180.0f;
      float endAngle = FTFixedToFloat(sweepGradient.end_angle) * 180.0f;
      // OpenType 1.9.1 adds a shift to the angle to ease specification of a 0 to 360
      // degree sweep.
      startAngle += 180.0f;
      endAngle += 180.0f;

      std::vector<float> stops;
      std::vector<uint32_t> colors;
      if (!fetchColorStops(sweepGradient.colorline.color_stop_iterator, stops, colors)) {
        return false;
      }

      if (stops.size() == 1) {
        paint->setColor(Uint32ToColor(colors[0]));
        return true;
      }

      // An opaque color is needed to ensure the gradient is not modulated by alpha.
      paint->setColor(Color::Black());

      // New (Var)SweepGradient implementation compliant with OpenType 1.9.1 from here.

      // The shader expects stops from 0 to 1, so we need to account for
      // minimum and maximum stop positions being different from 0 and
      // 1. We do that by scaling minimum and maximum stop positions to
      // the 0 to 1 interval and scaling the angles inverse proportionally.

      // 1) Scale angles to their equivalent positions if stops were from 0 to 1.

      float sectorAngle = endAngle - startAngle;
      SkTileMode tileMode = ToSkTileMode(sweepGradient.colorline.extend);
      if (sectorAngle == 0 && tileMode != SkTileMode::kClamp) {
        // "If the ColorLine's extend mode is reflect or repeat and start and end angle
        // are equal, nothing is drawn.".
        paint->setColor(Color::Transparent());
        return true;
      }

      float startAngleScaled = startAngle + sectorAngle * stops.front();
      float endAngleScaled = startAngle + sectorAngle * stops.back();

      // 2) Scale stops accordingly to 0 to 1 range.

      float colorStopRange = stops.back() - stops.front();
      if (colorStopRange == 0.f) {
        if (tileMode != SkTileMode::kClamp) {
          paint->setColor(Color::Transparent());
          return true;
        } else {
          // Insert duplicated fake color stop in pad case at +1.0f to feed the shader correct
          // values and enable painting a pad sweep gradient with two colors. Adding this stop
          // will paint the equivalent gradient, because: All font specified color stops are
          // in the same spot, mode is pad, so everything before this spot is painted with the
          // first color, everything after this spot is painted with the last color. Not
          // adding this stop will skip the projection and result in specifying non-normalized
          // color stops to the shader.
          stops.push_back(stops.back() + 1.0f);
          colors.push_back(colors.back());
          colorStopRange = 1.0f;
        }
      }

      float scaleFactor = 1 / colorStopRange;
      float startOffset = stops.front();

      for (float& stop : stops) {
        stop = (stop - startOffset) * scaleFactor;
      }

      /* https://docs.microsoft.com/en-us/typography/opentype/spec/colr#sweep-gradients
             * "The angles are expressed in counter-clockwise degrees from
             * the direction of the positive x-axis on the design
             * grid. [...]  The color line progresses from the start angle
             * to the end angle in the counter-clockwise direction;" -
             * Convert angles and stops from counter-clockwise to clockwise
             * for the shader if the gradient is not already reversed due to
             * start angle being larger than end angle. */
      startAngleScaled = 360.f - startAngleScaled;
      endAngleScaled = 360.f - endAngleScaled;
      if (startAngleScaled >= endAngleScaled) {
        std::swap(startAngleScaled, endAngleScaled);
        std::reverse(stops.begin(), stops.end());
        std::reverse(colors.begin(), colors.end());
        for (auto& stop : stops) {
          stop = 1.0f - stop;
        }
      }

      // paint->setShader(SkGradientShader::MakeSweep(
      //     center.x(), center.y(),
      //     colors.data(), SkColorSpace::MakeSRGB(), stops.data(), stops.size(),
      //     tileMode,
      //     startAngleScaled, endAngleScaled,
      //     SkGradientShader::Interpolation{
      //         SkGradientShader::Interpolation::InPremul::kNo,
      //         SkGradientShader::Interpolation::ColorSpace::kSRGB,
      //         SkGradientShader::Interpolation::HueMethod::kShorter
      //     },
      //     nullptr));

      return true;
    }
    default: {
      // SkASSERT(false);
      return false;
    }
  }
}

bool colrv1_draw_paint(Canvas* canvas, const std::vector<uint32_t>& palette,
                       const uint32_t foregroundColor, FT_Face face,
                       const FT_COLR_Paint& colrPaint) {
  switch (colrPaint.format) {
    case FT_COLR_PAINTFORMAT_GLYPH: {
      GlyphID glyphID = static_cast<GlyphID>(colrPaint.u.glyph.glyphID);
      Path path;
      /* TODO: Currently this call retrieves the path at units_per_em size. If we want to get
             * correct hinting for the scaled size under the transforms at this point in the color
             * glyph graph, we need to extract at least the requested glyph width and height and
             * pass that to the path generation. */
      if (!generateFacePathCOLRv1(face, glyphID, &path)) {
        return false;
      }
      // if constexpr (kSkShowTextBlitCoverage) {
      //     Paint highlight_paint;
      //     highlight_paint.setColor(Uint32ToColor(0x33FF0000));
      //     canvas->drawRect(path.getBounds(), highlight_paint);
      // }
      canvas->clipPath(path);
      return true;
    }
    case FT_COLR_PAINTFORMAT_SOLID:
    case FT_COLR_PAINTFORMAT_LINEAR_GRADIENT:
    case FT_COLR_PAINTFORMAT_RADIAL_GRADIENT:
    case FT_COLR_PAINTFORMAT_SWEEP_GRADIENT: {
      Paint skPaint;
      if (!colrv1_configure_skpaint(face, palette, foregroundColor, colrPaint, &skPaint)) {
        return false;
      }
      canvas->saveLayer(&skPaint);
      // canvas->drawPaint(skPaint);
      return true;
    }
    case FT_COLR_PAINTFORMAT_TRANSFORM:
    case FT_COLR_PAINTFORMAT_TRANSLATE:
    case FT_COLR_PAINTFORMAT_SCALE:
    case FT_COLR_PAINTFORMAT_ROTATE:
    case FT_COLR_PAINTFORMAT_SKEW:
      [[fallthrough]];  // Transforms handled in colrv1_transform.
    default:
      // SkASSERT(false);
      return false;
  }
}

bool colrv1_draw_glyph_with_path(Canvas* canvas, const std::vector<uint32_t>& palette,
                                 uint32_t foregroundColor, FT_Face face,
                                 const FT_COLR_Paint& glyphPaint, const FT_COLR_Paint& fillPaint) {
  // SkASSERT(glyphPaint.format == FT_COLR_PAINTFORMAT_GLYPH);
  // SkASSERT(fillPaint.format == FT_COLR_PAINTFORMAT_SOLID ||
  //          fillPaint.format == FT_COLR_PAINTFORMAT_LINEAR_GRADIENT ||
  //          fillPaint.format == FT_COLR_PAINTFORMAT_RADIAL_GRADIENT ||
  //          fillPaint.format == FT_COLR_PAINTFORMAT_SWEEP_GRADIENT);

  Paint skiaFillPaint;
  skiaFillPaint.setAntiAlias(true);
  if (!colrv1_configure_skpaint(face, palette, foregroundColor, fillPaint, &skiaFillPaint)) {
    return false;
  }
  GlyphID glyphID = static_cast<GlyphID>(glyphPaint.u.glyph.glyphID);
  Path path;
  /* TODO: Currently this call retrieves the path at units_per_em size. If we want to get
   * correct hinting for the scaled size under the transforms at this point in the color
   * glyph graph, we need to extract at least the requested glyph width and height and
   * pass that to the path generation. */
  if (!generateFacePathCOLRv1(face, glyphID, &path)) {
    return false;
  }
  // if constexpr (kSkShowTextBlitCoverage) {
  //   SkPaint highlightPaint;
  //   highlightPaint.setColor(0x33FF0000);
  //   canvas->drawRect(path.getBounds(), highlightPaint);
  // }
  canvas->drawPath(path, skiaFillPaint);
  return true;
}

bool colrv1_traverse_paint(Canvas* canvas, const std::vector<uint32_t>& palette,
                           const uint32_t foregroundColor, FT_Face face,
                           FT_OpaquePaint opaquePaint) {
  FT_COLR_Paint paint;
  if (!FT_Get_Paint(face, opaquePaint, &paint)) {
    return false;
  }

  // SkAutoCanvasRestore autoRestore(canvas, true /* doSave */);
  switch (paint.format) {
    case FT_COLR_PAINTFORMAT_COLR_LAYERS: {
      FT_LayerIterator& layerIterator = paint.u.colr_layers.layer_iterator;
      FT_OpaquePaint layerPaint{nullptr, 1};
      while (FT_Get_Paint_Layers(face, &layerIterator, &layerPaint)) {
        if (!colrv1_traverse_paint(canvas, palette, foregroundColor, face, layerPaint)) {
          return false;
        }
      }
      return true;
    }
    case FT_COLR_PAINTFORMAT_GLYPH:
      // Special case paint graph leaf situations to improve
      // performance. These are situations in the graph where a GlyphPaint
      // is followed by either a solid or a gradient fill. Here we can use
      // drawPath() + SkPaint directly which is faster than setting a
      // clipPath() followed by a drawPaint().
      FT_COLR_Paint fillPaint;
      if (!FT_Get_Paint(face, paint.u.glyph.paint, &fillPaint)) {
        return false;
      }
      if (fillPaint.format == FT_COLR_PAINTFORMAT_SOLID ||
          fillPaint.format == FT_COLR_PAINTFORMAT_LINEAR_GRADIENT ||
          fillPaint.format == FT_COLR_PAINTFORMAT_RADIAL_GRADIENT ||
          fillPaint.format == FT_COLR_PAINTFORMAT_SWEEP_GRADIENT) {
        return colrv1_draw_glyph_with_path(canvas, palette, foregroundColor, face, paint,
                                           fillPaint);
      }
      if (!colrv1_draw_paint(canvas, palette, foregroundColor, face, paint)) {
        return false;
      }
      return colrv1_traverse_paint(canvas, palette, foregroundColor, face, paint.u.glyph.paint);
    case FT_COLR_PAINTFORMAT_COLR_GLYPH:
      return colrv1_start_glyph(canvas, palette, foregroundColor, face, static_cast<uint16_t>(paint.u.colr_glyph.glyphID),
                                FT_COLOR_NO_ROOT_TRANSFORM);
    case FT_COLR_PAINTFORMAT_TRANSFORM:
      colrv1_transform(face, paint, canvas);
      return colrv1_traverse_paint(canvas, palette, foregroundColor, face, paint.u.transform.paint);
    case FT_COLR_PAINTFORMAT_TRANSLATE:
      colrv1_transform(face, paint, canvas);
      return colrv1_traverse_paint(canvas, palette, foregroundColor, face, paint.u.translate.paint);
    case FT_COLR_PAINTFORMAT_SCALE:
      colrv1_transform(face, paint, canvas);
      return colrv1_traverse_paint(canvas, palette, foregroundColor, face, paint.u.scale.paint);
    case FT_COLR_PAINTFORMAT_ROTATE:
      colrv1_transform(face, paint, canvas);
      return colrv1_traverse_paint(canvas, palette, foregroundColor, face, paint.u.rotate.paint);
    case FT_COLR_PAINTFORMAT_SKEW:
      colrv1_transform(face, paint, canvas);
      return colrv1_traverse_paint(canvas, palette, foregroundColor, face, paint.u.skew.paint);
    case FT_COLR_PAINTFORMAT_COMPOSITE: {
      // SkAutoCanvasRestore acr(canvas, false);
      canvas->saveLayer(nullptr);
      if (!colrv1_traverse_paint(canvas, palette, foregroundColor, face,
                                 paint.u.composite.backdrop_paint)) {
        return false;
      }
      Paint blendModePaint;
      blendModePaint.setBlendMode(ToBlendMode(paint.u.composite.composite_mode));
      canvas->saveLayer(&blendModePaint);
      return colrv1_traverse_paint(canvas, palette, foregroundColor, face,
                                   paint.u.composite.source_paint);
    }
    case FT_COLR_PAINTFORMAT_SOLID:
    case FT_COLR_PAINTFORMAT_LINEAR_GRADIENT:
    case FT_COLR_PAINTFORMAT_RADIAL_GRADIENT:
    case FT_COLR_PAINTFORMAT_SWEEP_GRADIENT: {
      return colrv1_draw_paint(canvas, palette, foregroundColor, face, paint);
    }
    default:
      return false;
  }
}

// using VisitedSet = THashSet<FT_OpaquePaint, OpaquePaintHasher>;
//
// void ComputeColrV1GlyphBoundingBox(FT_Face face, GlyphID glyphID,
//                                                            Rect* bounds) {
//   SkMatrix ctm;
//   *bounds = SkRect::MakeEmpty();
//   VisitedSet activePaints;
//   return colrv1_start_glyph_bounds(&ctm, bounds, face, glyphID,
//                                    FT_COLOR_INCLUDE_ROOT_TRANSFORM, &activePaints);
// }

uint32_t FTColorToUint32(FT_Color color) {
  return static_cast<uint32_t>((color.alpha << 24) | (color.red << 16) | (color.green << 8) | color.blue);
}

FTScalerContext::FTScalerContext(std::shared_ptr<Typeface> tf, float size)
    : ScalerContext(std::move(tf), size), textScale(size) {
  loadGlyphFlags |= FT_LOAD_NO_BITMAP;
  // Always using FT_LOAD_IGNORE_GLOBAL_ADVANCE_WIDTH to get correct
  // advances, as fontconfig and cairo do.
  loadGlyphFlags |= FT_LOAD_IGNORE_GLOBAL_ADVANCE_WIDTH;
  loadGlyphFlags |= FT_LOAD_TARGET_NORMAL;
  auto face = ftTypeface()->face;
  if (FT_IS_SCALABLE(face)) {
    loadGlyphFlags |= FT_LOAD_COLOR;
    FT_Palette_Data paletteData;
    if (FT_Palette_Data_Get(face, &paletteData)) {
      return;
    }
    FT_UShort basePaletteIndex = 0;
    FT_Color* ftPalette = nullptr;
    if (FT_Palette_Select(face, basePaletteIndex, &ftPalette)) {
      return;
    }
    auto platteCount = paletteData.num_palette_entries;
    colorPlatte.resize(platteCount);
    for (size_t i = 0; i < platteCount; i++) {
      colorPlatte[i] = FTColorToUint32(ftPalette[i]);
    }
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

    // Color bitmaps are supported.
    loadGlyphFlags |= FT_LOAD_COLOR;
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

bool GenerateGlyphPath(FT_Face face, Path* path) {
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
}

Rect FTScalerContext::getBounds(GlyphID glyphID, bool fauxBold, bool fauxItalic) const {
  std::lock_guard<std::mutex> autoLock(ftTypeface()->locker);
  return getBoundsInternal(glyphID, fauxBold, fauxItalic);
}

Rect FTScalerContext::getBoundsInternal(GlyphID glyphID, bool fauxBold, bool fauxItalic) const {
  auto bounds = Rect::MakeEmpty();
  if (setupSize(fauxItalic)) {
    return bounds;
  }
  auto face = ftTypeface()->face;
  bool haveLayers = false;
  if (FT_IS_SCALABLE(face)) {
    FT_OpaquePaint opaqueLayerPaint{nullptr, 1};
    if (FT_Get_Color_Glyph_Paint(face, glyphID, FT_COLOR_INCLUDE_ROOT_TRANSFORM,
                                 &opaqueLayerPaint)) {
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
        if (computeColrV1GlyphBoundingBox(face, glyphID, &bounds) || setupSize(fauxItalic)) {
          return bounds;
        }
      }
    }
    if (!haveLayers) {
      FT_LayerIterator layerIterator = {0, 0, nullptr};
      FT_UInt layerGlyphIndex;
      FT_UInt layerColorIndex;
      FT_Int32 flags = loadGlyphFlags;
      flags |= FT_LOAD_BITMAP_METRICS_ONLY;  // Don't decode any bitmaps.
      flags |= FT_LOAD_NO_BITMAP;            // Ignore embedded bitmaps.
      flags &= ~FT_LOAD_RENDER;              // Don't scan convert.
      flags &= ~FT_LOAD_COLOR;               // Ignore SVG.
      // For COLRv0 compute the glyph bounding box from the union of layer bounding boxes.
      while (FT_Get_Color_Glyph_Layer(face, glyphID, &layerGlyphIndex, &layerColorIndex,
                                      &layerIterator)) {
        haveLayers = true;
        if (FT_Load_Glyph(face, layerGlyphIndex, flags)) {
          return bounds;
        }

        Rect currentBounds;
        if (getBoundsOfCurrentOutlineGlyph(face->glyph, &currentBounds)) {
          bounds.join(currentBounds);
        }
      }
      // if (haveLayers) {
      //   mx.extraBits = ScalerContextBits::COLRv0;
      // }
    }
    // if (haveLayers) {g
    //   mx.maskFormat = SkMask::kARGB32_Format;
    //   mx.neverRequestPath = true;
    //   updateGlyphBoundsIfSubpixel(glyph, &bounds, this->isSubpixel());
    //   mx.bounds = bounds;
    // }
  }

  auto glyphFlags = loadGlyphFlags | static_cast<FT_Int32>(FT_LOAD_BITMAP_METRICS_ONLY);
  auto err = FT_Load_Glyph(face, glyphID, glyphFlags);
  if (err != FT_Err_Ok) {
    return bounds;
  }
  if (!haveLayers) {
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
      bounds.setLTRB(FDot6ToFloat(rect.xMin), -FDot6ToFloat(rect.yMax), FDot6ToFloat(rect.xMax),
                     -FDot6ToFloat(rect.yMin));
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
  auto bounds = getBoundsInternal(glyphID, false, false);
  return bounds;
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
  if (setupSize(false)) {
    return nullptr;
  }
  // colrv1 字体处理
  if (true) {
    auto glyphBounds = getBoundsInternal(glyphID, false, false);
    auto device = GLDevice::MakeWithFallback();
    if (!device) {
      return nullptr;
    }
    auto context = device->lockContext();
    if (!context) {
      return nullptr;
    }
    int width = static_cast<int>(ceilf(glyphBounds.width()));
    int height = static_cast<int>(ceilf(glyphBounds.height()));
    auto surface = Surface::Make(context, width, height);
    if (!surface) {
      device->unlock();
      return nullptr;
    }
    auto canvas = surface->getCanvas();
    canvas->clear();
    canvas->translate(-glyphBounds.left, -glyphBounds.top);
    colrv1_start_glyph(canvas, colorPlatte, 0x000000FF, ftTypeface()->face, glyphID, FT_COLOR_INCLUDE_ROOT_TRANSFORM);
    Bitmap bitmap(width, height);
    auto imageInfo = ImageInfo::Make(width, height, ColorType::RGBA_8888);
    if (!surface->readPixels(imageInfo, bitmap.lockPixels())) {
      device->unlock();
      return nullptr;
    }
    device->unlock();
    return bitmap.makeBuffer();
  }

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
