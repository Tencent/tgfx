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

#include "core/vectors/coregraphics/CGScalerContext.h"
#include "core/ScalerContext.h"
#include "core/utils/FauxBoldScale.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "platform/apple/BitmapContextUtil.h"
#include "tgfx/core/PathEffect.h"

namespace tgfx {

static CGAffineTransform GetTransform(bool fauxItalic) {
  static auto italicTransform =
      CGAffineTransformMake(1, 0, -static_cast<CGFloat>(ITALIC_SKEW), 1, 0, 0);
  static auto identityTransform = CGAffineTransformMake(1, 0, 0, 1, 0, 0);
  return fauxItalic ? italicTransform : identityTransform;
}

constexpr uint32_t TAG(char a, char b, char c, char d) noexcept {
  return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) |
         (static_cast<uint32_t>(c) << 8) | static_cast<uint32_t>(d);
}

static uint16_t ReadU16BE(const uint8_t* p) {
  return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

static uint32_t ReadU32BE(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

struct Strike {
  uint16_t ppem;
  uint16_t resolution;
  Strike(uint16_t ppem, uint16_t resolution) : ppem(ppem), resolution(resolution) {
  }
};

static Strike* FindClosestStrike(std::vector<Strike>& strikes, uint16_t targetPpem) {
  if (strikes.empty()) {
    return nullptr;
  }
  const auto it =
      std::lower_bound(strikes.begin(), strikes.end(), targetPpem,
                       [](const Strike& s, const auto target) { return s.ppem < target; });

  if (it == strikes.begin()) {
    return &strikes.front();
  }

  if (it == strikes.end()) {
    return &strikes.back();
  }

  const auto prev = it - 1;
  const auto diffPrev = targetPpem - prev->ppem;
  const auto diffCurr = it->ppem - targetPpem;

  return (diffPrev <= diffCurr) ? &(*prev) : &(*it);
}

static CTFontRef CreateBackingFont(CTFontRef ctFont, float textSize) {
  const CFDataRef sbix =
      CTFontCopyTable(ctFont, TAG('s', 'b', 'i', 'x'), kCTFontTableOptionNoOptions);
  if (sbix == nullptr) {
    return nullptr;
  }
  /** sbix table header
    struct sbixHeader {
      uint16_t version;
      uint16_t flags;
      uint32_t numStrikes;
    };
  */
  constexpr uint32_t SBIX_HEADER_SIZE = 8;
  auto bytes = CFDataGetBytePtr(sbix);
  const auto dataLength = static_cast<uint32_t>(CFDataGetLength(sbix));
  if (dataLength < SBIX_HEADER_SIZE) {
    CFRelease(sbix);
    return nullptr;
  }

  auto numStrikes = ReadU32BE(bytes + 4);
  if (dataLength < SBIX_HEADER_SIZE + numStrikes * 4) {
    numStrikes = (dataLength - SBIX_HEADER_SIZE) / 4;
  }

  std::vector<Strike> strikes;
  strikes.reserve(numStrikes);
  for (uint32_t i = 0; i < numStrikes; ++i) {
    const auto strikeOffset = ReadU32BE(bytes + SBIX_HEADER_SIZE + i * 4);
    if (strikeOffset + 4 > dataLength) {
      continue;
    }
    auto ppem = ReadU16BE(bytes + strikeOffset + 0);
    auto resolution = ReadU16BE(bytes + strikeOffset + 2);
    strikes.emplace_back(ppem, resolution);
  }
  CFRelease(sbix);

  std::sort(strikes.begin(), strikes.end(),
            [](const Strike& a, const Strike& b) { return a.ppem < b.ppem; });
  const auto strike = FindClosestStrike(strikes, static_cast<uint16_t>(textSize));
  if (strike == nullptr || FloatNearlyEqual(textSize, static_cast<float>(strike->ppem))) {
    return nullptr;
  }

  return CTFontCreateCopyWithAttributes(ctFont, static_cast<CGFloat>(strike->ppem), nullptr,
                                        nullptr);
}

CGScalerContext::CGScalerContext(std::shared_ptr<Typeface> tf, float size)
    : ScalerContext(std::move(tf), size) {
  CTFontRef font = std::static_pointer_cast<CGTypeface>(typeface)->ctFont;
  fauxBoldScale = FauxBoldScale(textSize);
  ctFont = CTFontCreateCopyWithAttributes(font, static_cast<CGFloat>(textSize), nullptr, nullptr);
  if (typeface->hasColor() || !typeface->hasOutlines()) {
    backingFont = CreateBackingFont(ctFont, textSize);
  }
  
  // Initialize FontMetrics in constructor to avoid runtime locking
  fontMetrics = computeFontMetrics();
}

CGScalerContext::~CGScalerContext() {
  if (ctFont) {
    CFRelease(ctFont);
  }
  if (backingFont) {
    CFRelease(backingFont);
  }
}

FontMetrics CGScalerContext::getFontMetrics() const {
  std::call_once(fontMetricsOnce, [this]() { fontMetrics = computeFontMetrics(); });
  return fontMetrics;
}

FontMetrics CGScalerContext::computeFontMetrics() const {
  FontMetrics metrics;
  auto theBounds = CTFontGetBoundingBox(ctFont);
  metrics.top = static_cast<float>(-CGRectGetMaxY(theBounds));
  metrics.ascent = static_cast<float>(-CTFontGetAscent(ctFont));
  metrics.descent = static_cast<float>(CTFontGetDescent(ctFont));
  metrics.bottom = static_cast<float>(-CGRectGetMinY(theBounds));
  metrics.leading = static_cast<float>(CTFontGetLeading(ctFont));
  metrics.xMin = static_cast<float>(CGRectGetMinX(theBounds));
  metrics.xMax = static_cast<float>(CGRectGetMaxX(theBounds));
  metrics.xHeight = static_cast<float>(CTFontGetXHeight(ctFont));
  metrics.capHeight = static_cast<float>(CTFontGetCapHeight(ctFont));
  metrics.underlineThickness = static_cast<float>(CTFontGetUnderlineThickness(ctFont));
  metrics.underlinePosition = -static_cast<float>(CTFontGetUnderlinePosition(ctFont));
  return metrics;
}

Rect CGScalerContext::getBounds(GlyphID glyphID, bool fauxBold, bool fauxItalic) const {
  CGBoundsKey key = {glyphID, fauxBold, fauxItalic};

  // Check cache first
  {
    std::lock_guard<std::mutex> cacheLock(boundsCacheMutex);
    auto it = boundsCache.find(key);
    if (it != boundsCache.end()) {
      return it->second;
    }
  }

  // Cache miss: compute bounds
  const auto cgGlyph = static_cast<CGGlyph>(glyphID);
  // Glyphs are always drawn from the horizontal origin. The caller must manually use the result
  // of CTFontGetVerticalTranslationsForGlyphs to calculate where to draw the glyph for vertical
  // glyphs. As a result, always get the horizontal bounds of a glyph and translate it if the
  // glyph is vertical. This avoids any disagreement between the various means of retrieving
  // vertical metrics.
  // CTFontGetBoundingRectsForGlyphs produces cgBounds in CG units (pixels, y up).
  CGRect cgBounds;
  CTFontGetBoundingRectsForGlyphs(ctFont, kCTFontOrientationHorizontal, &cgGlyph, &cgBounds, 1);
  auto transform = GetTransform(fauxItalic);
  cgBounds = CGRectApplyAffineTransform(cgBounds, transform);
  if (CGRectIsEmpty(cgBounds)) {
    // Store empty bounds in cache
    std::lock_guard<std::mutex> cacheLock(boundsCacheMutex);
    boundsCache[key] = {};
    return {};
  }
  // Convert cgBounds to Glyph units (pixels, y down).
  auto bounds = Rect::MakeXYWH(static_cast<float>(cgBounds.origin.x),
                               static_cast<float>(-cgBounds.origin.y - cgBounds.size.height),
                               static_cast<float>(cgBounds.size.width),
                               static_cast<float>(cgBounds.size.height));

  if (fauxBold) {
    auto fauxBoldSize = textSize * fauxBoldScale;
    bounds.outset(fauxBoldSize, fauxBoldSize);
  }
  bounds.roundOut();
  // Expand the bounds by 1 pixel, to give CG room for antialiasing.
  // Note that this outset is to allow room for LCD smoothed glyphs. However, the correct outset
  // is not currently known, as CG dilates the outlines by some percentage.
  // Note that if this context is A8 and not back-forming from LCD, there is no need to outset.
  bounds.outset(1.f, 1.f);

  // Store in cache
  {
    std::lock_guard<std::mutex> cacheLock(boundsCacheMutex);
    boundsCache[key] = bounds;
  }

  return bounds;
}

float CGScalerContext::getAdvance(GlyphID glyphID, bool verticalText) const {
  // Select cache based on text direction
  auto& cache = verticalText ? advanceCacheV : advanceCacheH;

  // Check cache first
  {
    std::lock_guard<std::mutex> cacheLock(advanceCacheMutex);
    auto it = cache.find(glyphID);
    if (it != cache.end()) {
      return it->second;
    }
  }

  // Cache miss: compute advance
  CGSize cgAdvance;
  if (verticalText) {
    CTFontGetAdvancesForGlyphs(ctFont, kCTFontOrientationVertical, &glyphID, &cgAdvance, 1);
    // Vertical advances are returned as widths instead of heights.
    std::swap(cgAdvance.width, cgAdvance.height);
  } else {
    CTFontGetAdvancesForGlyphs(ctFont, kCTFontOrientationHorizontal, &glyphID, &cgAdvance, 1);
  }
  float advance = verticalText ? static_cast<float>(cgAdvance.height) : static_cast<float>(cgAdvance.width);

  // Store in cache
  {
    std::lock_guard<std::mutex> cacheLock(advanceCacheMutex);
    cache[glyphID] = advance;
  }

  return advance;
}

Point CGScalerContext::getVerticalOffset(GlyphID glyphID) const {
  // CTFontGetVerticalTranslationsForGlyphs produces cgVertOffset in CG units (pixels, y up).
  CGSize cgVertOffset;
  CTFontGetVerticalTranslationsForGlyphs(ctFont, &glyphID, &cgVertOffset, 1);
  Point vertOffset = {static_cast<float>(cgVertOffset.width),
                      static_cast<float>(cgVertOffset.height)};
  // From CG units (pixels, y up) to Glyph units (pixels, y down).
  vertOffset.y = -vertOffset.y;
  return vertOffset;
}

class CTPathGeometrySink {
 public:
  Path path() {
    return _path;
  }

  static void ApplyElement(void* ctx, const CGPathElement* element) {
    CTPathGeometrySink& self = *static_cast<CTPathGeometrySink*>(ctx);
    CGPoint* points = element->points;
    switch (element->type) {
      case kCGPathElementMoveToPoint:
        self.started = false;
        self.current = points[0];
        break;
      case kCGPathElementAddLineToPoint:
        if (self.currentIsNot(points[0])) {
          self.goingTo(points[0]);
          self._path.lineTo(static_cast<float>(points[0].x), -static_cast<float>(points[0].y));
        }
        break;
      case kCGPathElementAddQuadCurveToPoint:
        if (self.currentIsNot(points[0]) || self.currentIsNot(points[1])) {
          self.goingTo(points[1]);
          self._path.quadTo(static_cast<float>(points[0].x), -static_cast<float>(points[0].y),
                            static_cast<float>(points[1].x), -static_cast<float>(points[1].y));
        }
        break;
      case kCGPathElementAddCurveToPoint:
        if (self.currentIsNot(points[0]) || self.currentIsNot(points[1]) ||
            self.currentIsNot(points[2])) {
          self.goingTo(points[2]);
          self._path.cubicTo(static_cast<float>(points[0].x), -static_cast<float>(points[0].y),
                             static_cast<float>(points[1].x), -static_cast<float>(points[1].y),
                             static_cast<float>(points[2].x), -static_cast<float>(points[2].y));
        }
        break;
      case kCGPathElementCloseSubpath:
        if (self.started) {
          self._path.close();
        }
        break;
      default:
        LOGE("Unknown path element!");
        break;
    }
  }

 private:
  void goingTo(const CGPoint pt) {
    if (!started) {
      started = true;
      _path.moveTo(static_cast<float>(current.x), -static_cast<float>(current.y));
    }
    current = pt;
  }

  bool currentIsNot(const CGPoint pt) const {
    return current.x != pt.x || current.y != pt.y;
  }

  Path _path = {};
  bool started = false;
  CGPoint current = {0, 0};
};

bool CGScalerContext::generatePath(GlyphID glyphID, bool fauxBold, bool fauxItalic,
                                   Path* path) const {
  auto fontFormat = CTFontCopyAttribute(ctFont, kCTFontFormatAttribute);
  if (!fontFormat) {
    return false;
  }
  SInt16 format;
  CFNumberGetValue(static_cast<CFNumberRef>(fontFormat), kCFNumberSInt16Type, &format);
  CFRelease(fontFormat);
  if (format == kCTFontFormatUnrecognized || format == kCTFontFormatBitmap) {
    return false;
  }
  auto transform = GetTransform(fauxItalic);
  auto cgGlyph = static_cast<CGGlyph>(glyphID);
  auto cgPath = CTFontCreatePathForGlyph(ctFont, cgGlyph, &transform);
  if (cgPath) {
    CTPathGeometrySink sink;
    CGPathApply(cgPath, &sink, CTPathGeometrySink::ApplyElement);
    *path = sink.path();
    CFRelease(cgPath);
    if (fauxBold) {
      auto strokePath = *path;
      Stroke stroke(textSize * fauxBoldScale);
      stroke.applyToPath(&strokePath);
      path->addPath(strokePath, PathOp::Union);
    }
  } else {
    path->reset();
  }
  return true;
}

Rect CGScalerContext::getImageTransform(GlyphID glyphID, bool fauxBold, const Stroke* stroke,
                                        Matrix* matrix) const {
  if (!hasColor() && (stroke != nullptr || fauxBold)) {
    return {};
  }

  const auto font = backingFont ? backingFont : ctFont;
  CGRect cgBounds;
  CTFontGetBoundingRectsForGlyphs(font, kCTFontOrientationHorizontal, &glyphID, &cgBounds, 1);
  if (CGRectIsEmpty(cgBounds)) {
    return {};
  }
  // Convert cgBounds to Glyph units (pixels, y down).
  auto bounds = Rect::MakeXYWH(static_cast<float>(cgBounds.origin.x),
                               static_cast<float>(-cgBounds.origin.y - cgBounds.size.height),
                               static_cast<float>(cgBounds.size.width),
                               static_cast<float>(cgBounds.size.height));
  bounds.roundOut();
  if (matrix) {
    matrix->setTranslate(bounds.left, bounds.top);
    if (backingFont != nullptr) {
      auto scale = textSize / getBackingSize();
      matrix->postScale(scale, scale);
    }
  }
  return bounds;
}

bool CGScalerContext::readPixels(GlyphID glyphID, bool fauxBold, const Stroke* stroke,
                                 const ImageInfo& dstInfo, void* dstPixels, const Point&) const {
  if (dstInfo.isEmpty() || dstPixels == nullptr) {
    return false;
  }
  auto bounds = getImageTransform(glyphID, fauxBold, stroke, nullptr);
  auto width = static_cast<int>(bounds.width());
  auto height = static_cast<int>(bounds.height());
  if (width <= 0 || height <= 0) {
    return false;
  }
  auto cgContext = CreateBitmapContext(dstInfo, dstPixels);
  if (cgContext == nullptr) {
    return false;
  }
  const auto font = backingFont ? backingFont : ctFont;
  CGContextClearRect(cgContext, CGRectMake(0, 0, width, height));
  CGContextSetBlendMode(cgContext, kCGBlendModeCopy);
  CGContextSetTextDrawingMode(cgContext, kCGTextFill);
  CGContextSetShouldAntialias(cgContext, true);
  CGContextSetShouldSmoothFonts(cgContext, true);
  auto point = CGPointMake(-bounds.left, bounds.bottom);
  CTFontDrawGlyphs(font, &glyphID, &point, 1, cgContext);
  CGContextRelease(cgContext);
  return true;
}

float CGScalerContext::getBackingSize() const {
  if (backingFont == nullptr) {
    return textSize;
  }
  return static_cast<float>(CTFontGetSize(backingFont));
}
}  // namespace tgfx
