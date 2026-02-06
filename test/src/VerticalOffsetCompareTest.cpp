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

#include <cmath>
#include <cstdio>
#include "tgfx/core/Font.h"
#include "tgfx/core/Typeface.h"
#include "utils/TestUtils.h"

#ifdef __APPLE__
#include <CoreText/CoreText.h>
#endif

namespace tgfx {

//=================================================================================================
// Test Configuration
//=================================================================================================

static const std::vector<char32_t> kTestChars = {
    U'A',  U'W',  U'g',   // Latin letters
    U'中', U'文', U'字',  // CJK characters
    U'!',  U'@',  U'('    // Symbols
};

static const std::vector<float> kTestFontSizes = {24.0f, 48.0f, 72.0f, 100.0f};

//=================================================================================================
// Helper Functions
//=================================================================================================

#ifdef __APPLE__
// Helper class to cache CoreText font resources for efficient repeated lookups.
class CoreTextFontCache {
 public:
  CoreTextFontCache(const std::string& fontPath, float fontSize) {
    CGDataProviderRef provider = CGDataProviderCreateWithFilename(fontPath.c_str());
    if (!provider) {
      return;
    }

    CGFontRef cgFont = CGFontCreateWithDataProvider(provider);
    CGDataProviderRelease(provider);
    if (!cgFont) {
      return;
    }

    ctFont = CTFontCreateWithGraphicsFont(cgFont, fontSize, nullptr, nullptr);
    CGFontRelease(cgFont);
  }

  ~CoreTextFontCache() {
    if (ctFont) {
      CFRelease(ctFont);
    }
  }

  // Non-copyable
  CoreTextFontCache(const CoreTextFontCache&) = delete;
  CoreTextFontCache& operator=(const CoreTextFontCache&) = delete;

  bool isValid() const {
    return ctFont != nullptr;
  }

  Point getVerticalOffset(GlyphID glyphID) const {
    if (!ctFont) {
      return {0, 0};
    }
    CGSize offset;
    CTFontGetVerticalTranslationsForGlyphs(ctFont, &glyphID, &offset, 1);
    // Convert from CG coordinates (y-up) to tgfx coordinates (y-down)
    return {static_cast<float>(offset.width), static_cast<float>(-offset.height)};
  }

 private:
  CTFontRef ctFont = nullptr;
};
#endif

static std::string Char32ToUTF8(char32_t ch) {
  std::string result;
  if (ch < 0x80) {
    // 1-byte: 0xxxxxxx
    result += static_cast<char>(ch);
  } else if (ch < 0x800) {
    // 2-byte: 110xxxxx 10xxxxxx
    result += static_cast<char>(0xC0 | (ch >> 6));
    result += static_cast<char>(0x80 | (ch & 0x3F));
  } else if (ch < 0x10000) {
    // 3-byte: 1110xxxx 10xxxxxx 10xxxxxx
    result += static_cast<char>(0xE0 | (ch >> 12));
    result += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
    result += static_cast<char>(0x80 | (ch & 0x3F));
  } else if (ch < 0x110000) {
    // 4-byte: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx (for Unicode beyond BMP, e.g., emojis)
    result += static_cast<char>(0xF0 | (ch >> 18));
    result += static_cast<char>(0x80 | ((ch >> 12) & 0x3F));
    result += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
    result += static_cast<char>(0x80 | (ch & 0x3F));
  }
  return result;
}

//=================================================================================================
// Part 1: Numeric Tests (Lightweight, Cross-platform)
//=================================================================================================

/**
 * Core numeric comparison test.
 * On macOS: compares FreeType vs CoreText, verifies difference < 1 pixel.
 * On other platforms: verifies API returns reasonable values.
 */
TGFX_TEST(VerticalOffsetCompare, NumericComparison) {
  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr);

  constexpr float fontSize = 100.0f;
  Font font(typeface, fontSize);

  printf("\n=== Vertical Offset Numeric Test ===\n");
  printf("Font: NotoSansSC-Regular.otf, Size: %.1f\n\n", fontSize);

#ifdef __APPLE__
  auto fontPath = ProjectPath::Absolute("resources/font/NotoSansSC-Regular.otf");
  CoreTextFontCache ctFontCache(fontPath, fontSize);
  ASSERT_TRUE(ctFontCache.isValid()) << "Failed to create CoreText font";

  printf("Char | GlyphID | FreeType      | CoreText      | Diff\n");
  printf("-----|---------|---------------|---------------|----------\n");

  float maxDiffX = 0, maxDiffY = 0;

  for (char32_t ch : kTestChars) {
    auto glyphID = typeface->getGlyphID(static_cast<Unichar>(ch));
    Point ftOffset = font.getVerticalOffset(glyphID);
    Point ctOffset = ctFontCache.getVerticalOffset(glyphID);

    float diffX = std::abs(ftOffset.x - ctOffset.x);
    float diffY = std::abs(ftOffset.y - ctOffset.y);
    maxDiffX = std::max(maxDiffX, diffX);
    maxDiffY = std::max(maxDiffY, diffY);

    printf("%-4s | %-7u | (%6.2f,%6.2f) | (%6.2f,%6.2f) | (%.2f,%.2f)\n", Char32ToUTF8(ch).c_str(),
           glyphID, ftOffset.x, ftOffset.y, ctOffset.x, ctOffset.y, diffX, diffY);
  }

  printf("\nMax difference: X=%.3f, Y=%.3f pixels\n", maxDiffX, maxDiffY);

  EXPECT_LT(maxDiffX, 1.0f) << "X offset difference too large";
  EXPECT_LT(maxDiffY, 1.0f) << "Y offset difference too large";

#else
  printf("CoreText not available. Testing FreeType only:\n\n");

  for (char32_t ch : kTestChars) {
    auto glyphID = typeface->getGlyphID(static_cast<Unichar>(ch));
    Point offset = font.getVerticalOffset(glyphID);

    EXPECT_GE(offset.x, -200.0f);
    EXPECT_LE(offset.x, 200.0f);
    EXPECT_GE(offset.y, -200.0f);
    EXPECT_LE(offset.y, 200.0f);

    printf("U+%04X: offset (%.2f, %.2f)\n", static_cast<uint32_t>(ch), offset.x, offset.y);
  }
#endif
}

/**
 * Regression test: Ensures consistent behavior across font sizes.
 */
TGFX_TEST(VerticalOffsetCompare, FontSizeConsistency) {
  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr);

  auto glyphID = typeface->getGlyphID(static_cast<Unichar>(U'中'));

  for (float fontSize : kTestFontSizes) {
    Font font(typeface, fontSize);
    Point offset = font.getVerticalOffset(glyphID);

    // Verify no NaN or Inf
    EXPECT_FALSE(std::isnan(offset.x)) << "X is NaN at size " << fontSize;
    EXPECT_FALSE(std::isnan(offset.y)) << "Y is NaN at size " << fontSize;
    EXPECT_FALSE(std::isinf(offset.x)) << "X is Inf at size " << fontSize;
    EXPECT_FALSE(std::isinf(offset.y)) << "Y is Inf at size " << fontSize;

    // Reasonable range (scales with font size)
    float maxExpected = fontSize * 2.0f;
    EXPECT_GE(offset.x, -maxExpected);
    EXPECT_LE(offset.x, maxExpected);
    EXPECT_GE(offset.y, -maxExpected);
    EXPECT_LE(offset.y, maxExpected);
  }
}

//=================================================================================================
// Part 2: Visualization Tests (For manual inspection, no baseline dependency)
//=================================================================================================

// Layout constants for visualization
static constexpr int kCharSpacing = 160;
static constexpr int kRowSpacing = 180;
static constexpr int kMarginLeft = 120;
static constexpr int kMarginTop = 140;
static constexpr int kTitleHeight = 50;

// Colors for visualization
static const Color kBaselineColor = Color::FromRGBA(200, 200, 200, 255);
static const Color kArrowColor = Color::FromRGBA(0, 180, 0, 255);
static const Color kGlyphBeforeColor = Color::FromRGBA(0, 0, 0, 77);
static const Color kGlyphAfterColor = Color::Black();

static void DrawArrow(Canvas* canvas, const Point& start, const Point& end, const Paint& paint) {
  canvas->drawLine(start, end, paint);

  float dx = end.x - start.x;
  float dy = end.y - start.y;
  float length = std::sqrt(dx * dx + dy * dy);
  if (length < 5.0f) {
    return;
  }

  float nx = dx / length;
  float ny = dy / length;
  float headLen = std::min(10.0f, length * 0.3f);
  float headW = headLen * 0.5f;

  Point base = {end.x - nx * headLen, end.y - ny * headLen};
  canvas->drawLine(end, {base.x - ny * headW, base.y + nx * headW}, paint);
  canvas->drawLine(end, {base.x + ny * headW, base.y - nx * headW}, paint);
}

static void DrawGlyphCell(Canvas* canvas, const Font& font, GlyphID glyphID, float cx, float cy,
                          const Point& offset) {
  Paint linePaint;
  linePaint.setStyle(PaintStyle::Stroke);
  linePaint.setStrokeWidth(1.0f);
  linePaint.setColor(kBaselineColor);
  canvas->drawLine(cx - 60.0f, cy, cx + 80.0f, cy, linePaint);
  canvas->drawLine(cx, cy - 80.0f, cx, cy + 80.0f, linePaint);

  Paint beforePaint;
  beforePaint.setColor(kGlyphBeforeColor);
  Point beforePos = {cx, cy};
  canvas->drawGlyphs(&glyphID, &beforePos, 1, font, beforePaint);

  if (std::abs(offset.x) > 0.5f || std::abs(offset.y) > 0.5f) {
    Paint arrowPaint;
    arrowPaint.setStyle(PaintStyle::Stroke);
    arrowPaint.setStrokeWidth(2.0f);
    arrowPaint.setColor(kArrowColor);
    DrawArrow(canvas, {cx, cy}, {cx + offset.x, cy + offset.y}, arrowPaint);
  }

  Paint afterPaint;
  afterPaint.setColor(kGlyphAfterColor);
  Point afterPos = {cx + offset.x, cy + offset.y};
  canvas->drawGlyphs(&glyphID, &afterPos, 1, font, afterPaint);

  Paint dotPaint;
  dotPaint.setStyle(PaintStyle::Fill);
  dotPaint.setColor(Color::FromRGBA(255, 0, 0, 255));
  canvas->drawCircle(cx, cy, 4.0f, dotPaint);
  dotPaint.setColor(Color::FromRGBA(0, 0, 255, 255));
  canvas->drawCircle(cx + offset.x, cy + offset.y, 4.0f, dotPaint);
}

/**
 * Visual test: Generates offset visualization image.
 * Output saved to test/out/ for manual inspection.
 * Does NOT use baseline comparison system.
 */
TGFX_TEST(VerticalOffsetCompare, Visualization) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr);

  // 3x3 grid for test chars + 1 row for font size variation
  int totalWidth = kMarginLeft + kCharSpacing * 3 + 60;
  int totalHeight = kMarginTop + kRowSpacing * 4 + kTitleHeight;

  auto surface = Surface::Make(context, totalWidth, totalHeight);
  ASSERT_TRUE(surface != nullptr);

  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  Font font(typeface, 100.0f);

  // Draw 3x3 character grid
  for (size_t i = 0; i < kTestChars.size(); ++i) {
    int row = static_cast<int>(i / 3);
    int col = static_cast<int>(i % 3);
    float x = static_cast<float>(kMarginLeft + col * kCharSpacing);
    float y = static_cast<float>(kMarginTop + row * kRowSpacing);

    auto glyphID = typeface->getGlyphID(static_cast<Unichar>(kTestChars[i]));
    Point offset = font.getVerticalOffset(glyphID);
    DrawGlyphCell(canvas, font, glyphID, x, y, offset);
  }

  // Draw font size variation row
  float sizeRowY = static_cast<float>(kMarginTop + 3 * kRowSpacing);
  auto testGlyph = typeface->getGlyphID(static_cast<Unichar>(U'W'));

  for (size_t i = 0; i < kTestFontSizes.size() && i < 3; ++i) {
    float x = static_cast<float>(kMarginLeft + static_cast<int>(i) * kCharSpacing);
    Font sizedFont(typeface, kTestFontSizes[i]);
    Point offset = sizedFont.getVerticalOffset(testGlyph);
    DrawGlyphCell(canvas, sizedFont, testGlyph, x, sizeRowY, offset);
  }

  // Save to test output directory (not baseline comparison)
  Bitmap bitmap(surface->width(), surface->height(), false, false);
  Pixmap pixmap(bitmap);
  ASSERT_TRUE(surface->readPixels(pixmap.info(), pixmap.writablePixels()));

  SaveImage(pixmap, "VerticalOffsetCompare/Visualization");
  printf("\nVisualization saved to: test/out/VerticalOffsetCompare/Visualization.webp\n");
}

#ifdef __APPLE__
/**
 * macOS-only: Side-by-side comparison of FreeType vs CoreText.
 * Generates a three-panel image for visual diff inspection.
 */
TGFX_TEST(VerticalOffsetCompare, DiffVisualization) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr);

  constexpr float fontSize = 100.0f;
  Font font(typeface, fontSize);
  auto fontPath = ProjectPath::Absolute("resources/font/NotoSansSC-Regular.otf");
  CoreTextFontCache ctFontCache(fontPath, fontSize);
  ASSERT_TRUE(ctFontCache.isValid()) << "Failed to create CoreText font";

  int panelWidth = kMarginLeft + kCharSpacing * 3 + 60;
  int panelHeight = kMarginTop + kRowSpacing * 3 + kTitleHeight;
  int totalWidth = panelWidth * 3;  // FT | CT | Overlay

  auto surface = Surface::Make(context, totalWidth, panelHeight);
  ASSERT_TRUE(surface != nullptr);

  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  // Panel separators
  Paint sepPaint;
  sepPaint.setStyle(PaintStyle::Stroke);
  sepPaint.setStrokeWidth(2.0f);
  sepPaint.setColor(Color::FromRGBA(100, 100, 100, 255));
  canvas->drawLine(static_cast<float>(panelWidth), 0, static_cast<float>(panelWidth),
                   static_cast<float>(panelHeight), sepPaint);
  canvas->drawLine(static_cast<float>(panelWidth * 2), 0, static_cast<float>(panelWidth * 2),
                   static_cast<float>(panelHeight), sepPaint);

  // Panel 1: FreeType implementation (blue)
  Color ftPanelColor = Color::FromRGBA(0, 0, 255, 255);
  for (size_t i = 0; i < kTestChars.size(); ++i) {
    int row = static_cast<int>(i / 3);
    int col = static_cast<int>(i % 3);
    float x = static_cast<float>(kMarginLeft + col * kCharSpacing);
    float y = static_cast<float>(kMarginTop + row * kRowSpacing);

    auto glyphID = typeface->getGlyphID(static_cast<Unichar>(kTestChars[i]));
    Point offset = font.getVerticalOffset(glyphID);

    // Draw baselines
    Paint linePaint;
    linePaint.setStyle(PaintStyle::Stroke);
    linePaint.setStrokeWidth(1.0f);
    linePaint.setColor(kBaselineColor);
    canvas->drawLine(x - 60.0f, y, x + 80.0f, y, linePaint);
    canvas->drawLine(x, y - 80.0f, x, y + 80.0f, linePaint);

    // Draw glyph in blue
    Paint glyphPaint;
    glyphPaint.setColor(ftPanelColor);
    Point pos = {x + offset.x, y + offset.y};
    canvas->drawGlyphs(&glyphID, &pos, 1, font, glyphPaint);

    // Origin dot
    Paint dotPaint;
    dotPaint.setStyle(PaintStyle::Fill);
    dotPaint.setColor(ftPanelColor);
    canvas->drawCircle(x + offset.x, y + offset.y, 4.0f, dotPaint);
  }

  // Panel 2: CoreText implementation (red)
  Color ctPanelColor = Color::FromRGBA(255, 0, 0, 255);
  for (size_t i = 0; i < kTestChars.size(); ++i) {
    int row = static_cast<int>(i / 3);
    int col = static_cast<int>(i % 3);
    float x = static_cast<float>(panelWidth + kMarginLeft + col * kCharSpacing);
    float y = static_cast<float>(kMarginTop + row * kRowSpacing);

    auto glyphID = typeface->getGlyphID(static_cast<Unichar>(kTestChars[i]));
    Point offset = ctFontCache.getVerticalOffset(glyphID);

    // Draw baselines
    Paint linePaint;
    linePaint.setStyle(PaintStyle::Stroke);
    linePaint.setStrokeWidth(1.0f);
    linePaint.setColor(kBaselineColor);
    canvas->drawLine(x - 60.0f, y, x + 80.0f, y, linePaint);
    canvas->drawLine(x, y - 80.0f, x, y + 80.0f, linePaint);

    // Draw glyph in red
    Paint glyphPaint;
    glyphPaint.setColor(ctPanelColor);
    Point pos = {x + offset.x, y + offset.y};
    canvas->drawGlyphs(&glyphID, &pos, 1, font, glyphPaint);

    // Origin dot
    Paint dotPaint;
    dotPaint.setStyle(PaintStyle::Fill);
    dotPaint.setColor(ctPanelColor);
    canvas->drawCircle(x + offset.x, y + offset.y, 4.0f, dotPaint);
  }

  // Panel 3: Overlay (FT blue, CT red)
  Color ftColor = Color::FromRGBA(0, 0, 255, 180);
  Color ctColor = Color::FromRGBA(255, 0, 0, 180);

  for (size_t i = 0; i < kTestChars.size(); ++i) {
    int row = static_cast<int>(i / 3);
    int col = static_cast<int>(i % 3);
    float x = static_cast<float>(panelWidth * 2 + kMarginLeft + col * kCharSpacing);
    float y = static_cast<float>(kMarginTop + row * kRowSpacing);

    auto glyphID = typeface->getGlyphID(static_cast<Unichar>(kTestChars[i]));
    Point ftOffset = font.getVerticalOffset(glyphID);
    Point ctOffset = ctFontCache.getVerticalOffset(glyphID);

    // Baselines
    Paint linePaint;
    linePaint.setStyle(PaintStyle::Stroke);
    linePaint.setStrokeWidth(1.0f);
    linePaint.setColor(kBaselineColor);
    canvas->drawLine(x - 60.0f, y, x + 80.0f, y, linePaint);
    canvas->drawLine(x, y - 80.0f, x, y + 80.0f, linePaint);

    // CT glyph (red)
    Paint ctPaint;
    ctPaint.setColor(ctColor);
    Point ctPos = {x + ctOffset.x, y + ctOffset.y};
    canvas->drawGlyphs(&glyphID, &ctPos, 1, font, ctPaint);

    // FT glyph (blue)
    Paint ftPaint;
    ftPaint.setColor(ftColor);
    Point ftPos = {x + ftOffset.x, y + ftOffset.y};
    canvas->drawGlyphs(&glyphID, &ftPos, 1, font, ftPaint);

    // Origin dots
    Paint dotPaint;
    dotPaint.setStyle(PaintStyle::Fill);
    dotPaint.setColor(ctColor);
    canvas->drawCircle(x + ctOffset.x, y + ctOffset.y, 4.0f, dotPaint);
    dotPaint.setColor(ftColor);
    canvas->drawCircle(x + ftOffset.x, y + ftOffset.y, 4.0f, dotPaint);
  }

  // Titles
  Font titleFont(typeface, 18.0f);
  Paint titlePaint;
  titlePaint.setColor(Color::Black());
  canvas->drawSimpleText("FreeType (Blue)", static_cast<float>(panelWidth / 2 - 60),
                         static_cast<float>(panelHeight - 15), titleFont, titlePaint);
  canvas->drawSimpleText("CoreText (Red)", static_cast<float>(panelWidth + panelWidth / 2 - 60),
                         static_cast<float>(panelHeight - 15), titleFont, titlePaint);
  canvas->drawSimpleText("FT(Blue) vs CT(Red)",
                         static_cast<float>(panelWidth * 2 + panelWidth / 2 - 80),
                         static_cast<float>(panelHeight - 15), titleFont, titlePaint);

  // Save output
  Bitmap bitmap(surface->width(), surface->height(), false, false);
  Pixmap pixmap(bitmap);
  ASSERT_TRUE(surface->readPixels(pixmap.info(), pixmap.writablePixels()));

  SaveImage(pixmap, "VerticalOffsetCompare/DiffVisualization");
  printf("\nDiff visualization saved to: test/out/VerticalOffsetCompare/DiffVisualization.webp\n");
}
#endif  // __APPLE__

}  // namespace tgfx
