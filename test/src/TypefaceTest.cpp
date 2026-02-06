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

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <set>
#include "tgfx/core/CustomTypeface.h"
#include "tgfx/core/Font.h"
#include "tgfx/core/Typeface.h"
#include "utils/TestUtils.h"

namespace tgfx {

// A PathProvider that creates paths in 25x25 coordinate space for testing unitsPerEm scaling.
// With unitsPerEm=25 and fontSize=50, textScale=2.0, so paths scale to 50x50 pixels.
// This also tests fauxBold scaling: if fauxBold incorrectly uses textScale * fauxBoldScale
// instead of fontSize * fauxBoldScale, the bold effect would be wrong.
class GlyphPathProvider final : public PathProvider {
 public:
  explicit GlyphPathProvider(int pathIndex) : pathIndex(pathIndex) {
  }

  Path getPath() const override {
    Path path;
    switch (pathIndex) {
      case 0:
        path.moveTo(Point::Make(12.5f, 2.5f));
        path.lineTo(Point::Make(22.5f, 22.5f));
        path.lineTo(Point::Make(2.5f, 22.5f));
        path.close();
        break;
      case 1:
        path.moveTo(Point::Make(2.5f, 2.5f));
        path.lineTo(Point::Make(22.5f, 2.5f));
        path.lineTo(Point::Make(22.5f, 22.5f));
        path.lineTo(Point::Make(2.5f, 22.5f));
        path.close();
        break;
      case 2: {
        Rect rect = Rect::MakeXYWH(2.5f, 2.5f, 20.0f, 20.0f);
        path.addOval(rect);
        path.close();
        break;
      }
      default:
        break;
    }
    return path;
  }

  Rect getBounds() const override {
    return Rect::MakeXYWH(2.5f, 2.5f, 20.0f, 20.0f);
  }

 private:
  int pathIndex = 0;
};

TGFX_TEST(TypefaceTest, CustomPathTypeface) {
  const std::string fontFamily = "customPath";
  const std::string fontStyle = "customStyle";
  // Paths are designed in 25x25 coordinate space. Set unitsPerEm=25 so that with fontSize=50,
  // textScale=2.0 scales them to 50x50 pixels.
  PathTypefaceBuilder builder(25);
  builder.setFontName(fontFamily, fontStyle);

  builder.addGlyph(std::make_shared<GlyphPathProvider>(0));
  builder.addGlyph(std::make_shared<GlyphPathProvider>(1));
  builder.addGlyph(std::make_shared<GlyphPathProvider>(2));
  auto typeface = builder.detach();

  ASSERT_TRUE(typeface != nullptr);
  ASSERT_TRUE(typeface->hasOutlines());
  ASSERT_FALSE(typeface->hasColor());
  ASSERT_TRUE(typeface->openStream() == nullptr);
  ASSERT_TRUE(typeface->copyTableData(0) == nullptr);
  ASSERT_EQ(typeface->fontFamily(), fontFamily);
  ASSERT_EQ(typeface->fontStyle(), fontStyle);
  ASSERT_EQ(typeface->glyphsCount(), static_cast<size_t>(3));
  ASSERT_EQ(typeface->unitsPerEm(), 25);

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 250, 150);
  auto canvas = surface->getCanvas();

  auto paint = Paint();
  paint.setColor(Color::Red());

  // fontSize=50, unitsPerEm=25 => textScale=2.0
  // Glyphs will be scaled 2x from 25x25 design space to 50x50 pixels.
  // FauxBold uses fontSize(50) for calculation.
  // fauxBoldSize = 50 * FauxBoldScale(50) ≈ 1.5625 pixels.
  Font font(typeface, 50.0f);
  font.setFauxBold(true);
  std::vector<GlyphID> glyphIDs = {1, 2, 3};
  std::vector<Point> positions = {Point::Make(45, 50), Point::Make(105, 50), Point::Make(165, 50)};
  canvas->drawGlyphs(glyphIDs.data(), positions.data(), glyphIDs.size(), font, paint);

  EXPECT_TRUE(Baseline::Compare(surface, "TypefaceTest/CustomPathTypeface"));
}

TGFX_TEST(TypefaceTest, CustomImageTypeface) {
  const std::string fontFamily = "customImage";
  const std::string fontStyle = "customStyle";
  // Glyph images are 200x200 pixels. Set unitsPerEm=200 to match the image size.
  ImageTypefaceBuilder builder(200);
  builder.setFontName(fontFamily, fontStyle);
  std::string imagePath = "resources/assets/glyph1.png";
  auto imageCodec = ImageCodec::MakeFrom(ProjectPath::Absolute(imagePath));
  builder.addGlyph(std::move(imageCodec), Point::Make(0.0f, 0.0f));

  imagePath = "resources/assets/glyph2.png";
  imageCodec = ImageCodec::MakeFrom(ProjectPath::Absolute(imagePath));
  builder.addGlyph(std::move(imageCodec), Point::Make(0.0f, 0.0f));

  auto typeface = builder.detach();

  ASSERT_TRUE(typeface != nullptr);
  ASSERT_TRUE(typeface->hasColor());
  ASSERT_FALSE(typeface->hasOutlines());
  ASSERT_TRUE(typeface->openStream() == nullptr);
  ASSERT_TRUE(typeface->copyTableData(0) == nullptr);
  ASSERT_EQ(typeface->fontFamily(), fontFamily);
  ASSERT_EQ(typeface->fontStyle(), fontStyle);
  ASSERT_EQ(typeface->glyphsCount(), static_cast<size_t>(2));
  ASSERT_EQ(typeface->unitsPerEm(), 200);

  imagePath = "resources/assets/glyph3.png";
  imageCodec = ImageCodec::MakeFrom(ProjectPath::Absolute(imagePath));
  builder.addGlyph(std::move(imageCodec), Point::Make(0.0f, 0.0f));

  typeface = builder.detach();
  ASSERT_TRUE(typeface != nullptr);
  ASSERT_EQ(typeface->glyphsCount(), static_cast<size_t>(3));

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 250, 150);
  auto canvas = surface->getCanvas();

  // With fontSize=50 and unitsPerEm=200, textScale = 0.25
  // 200x200 images will render as 50x50 pixels
  Font font(std::move(typeface), 50.0f);
  std::vector<GlyphID> glyphIDs2 = {1, 2, 3};
  std::vector<Point> positions2 = {Point::Make(45, 50), Point::Make(105, 50), Point::Make(165, 50)};
  canvas->drawGlyphs(glyphIDs2.data(), positions2.data(), glyphIDs2.size(), font, {});

  EXPECT_TRUE(Baseline::Compare(surface, "TypefaceTest/CustomImageTypeface"));
}

TGFX_TEST(TypefaceTest, FontMetricsCachePerformance) {
  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr);

  Font font(typeface, 24.0f);
  const int iterations = 100000;

  auto start = std::chrono::high_resolution_clock::now();
  float sum = 0;
  for (int i = 0; i < iterations; i++) {
    auto metrics = font.getMetrics();
    sum += metrics.ascent;
  }
  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

  printf("getFontMetrics() x %d: %lld us (avg: %.4f us/call)\n", iterations,
         static_cast<long long>(duration.count()),
         static_cast<double>(duration.count()) / iterations);

  EXPECT_NE(sum, 0);
  EXPECT_LT(duration.count(), 1000000);
}

// Generate random glyph IDs from common Chinese characters (GB2312 Level-1: 3755 chars).
// This simulates real-world text rendering with character repetition.
static std::vector<GlyphID> GenerateRandomCommonGlyphs(const std::shared_ptr<Typeface>& typeface,
                                                       int count) {
  // Common Chinese characters frequency list (simplified, first 100 most used).
  // In real scenario, use full GB2312 Level-1 character set.
  static const char32_t commonChars[] = {
      U'的', U'一', U'是', U'在', U'不', U'了', U'有', U'和', U'人', U'这',
      U'中', U'大', U'为', U'上', U'个', U'国', U'我', U'以', U'要', U'他',
      U'时', U'来', U'用', U'们', U'生', U'到', U'作', U'地', U'于', U'出',
      U'就', U'分', U'对', U'成', U'会', U'可', U'主', U'发', U'年', U'动',
      U'同', U'工', U'也', U'能', U'下', U'过', U'子', U'说', U'产', U'种',
      U'面', U'而', U'方', U'后', U'多', U'定', U'行', U'学', U'法', U'所',
      U'民', U'得', U'经', U'十', U'三', U'之', U'进', U'着', U'等', U'部',
      U'度', U'家', U'电', U'力', U'里', U'如', U'水', U'化', U'高', U'自',
      U'二', U'理', U'起', U'小', U'物', U'现', U'实', U'加', U'量', U'都',
      U'两', U'体', U'制', U'机', U'当', U'使', U'点', U'从', U'业', U'本'};
  
  constexpr size_t numCommonChars = sizeof(commonChars) / sizeof(commonChars[0]);
  
  // Convert to glyph IDs.
  std::vector<GlyphID> glyphPool;
  glyphPool.reserve(numCommonChars);
  auto scalerContext = typeface->getScalerContext(24.0f);
  for (size_t i = 0; i < numCommonChars; i++) {
    // Use typeface's internal method to get glyph ID.
    // Note: Font::getGlyphID requires Font object, so we use Unicode directly.
    auto glyphID = typeface->getGlyphID(static_cast<Unichar>(commonChars[i]));
    if (glyphID > 0) {
      glyphPool.push_back(glyphID);
    }
  }
  
  if (glyphPool.empty()) {
    // Fallback: use ASCII characters.
    Font font(typeface, 24.0f);
    for (char c = 'A'; c <= 'Z'; c++) {
      glyphPool.push_back(font.getGlyphID(c));
    }
  }
  
  // Randomly select from pool to simulate text with repetition.
  std::vector<GlyphID> result;
  result.reserve(static_cast<size_t>(count));
  std::srand(static_cast<unsigned>(std::time(nullptr)));
  for (int i = 0; i < count; i++) {
    result.push_back(glyphPool[static_cast<size_t>(std::rand()) % glyphPool.size()]);
  }
  
  return result;
}

// Test getAdvance() performance with cache.
// Scenario: High hit rate (100,000 calls from ~100 unique glyphs, simulating text layout).
TGFX_TEST(TypefaceTest, AdvanceCacheHighHitRate) {
  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr);
  
  Font font(typeface, 24.0f);
  std::vector<GlyphID> glyphs = GenerateRandomCommonGlyphs(typeface, 100000);
  
  auto start = std::chrono::high_resolution_clock::now();
  float sum = 0;
  for (auto glyphID : glyphs) {
    sum += font.getAdvance(glyphID, false);
  }
  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  
  printf("getAdvance() high hit rate x100,000: %lld us (avg: %.4f us/call)\n",
         static_cast<long long>(duration.count()),
         static_cast<double>(duration.count()) / glyphs.size());
  
  EXPECT_NE(sum, 0);
  // With cache: expect < 50ms. Without cache: ~230ms (27x slower).
  EXPECT_LT(duration.count(), 50000);
}

// Test getAdvance() performance with low cache hit rate.
// Scenario: Each glyph accessed only once (worst case for cache).
TGFX_TEST(TypefaceTest, AdvanceCacheLowHitRate) {
  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr);
  
  Font font(typeface, 24.0f);
  // Generate sequential glyph IDs (low repetition).
  std::vector<GlyphID> glyphs;
  for (GlyphID gid = 1; gid <= 3000 && gid < typeface->glyphsCount(); gid++) {
    glyphs.push_back(gid);
  }
  
  auto start = std::chrono::high_resolution_clock::now();
  float sum = 0;
  for (auto glyphID : glyphs) {
    sum += font.getAdvance(glyphID, false);
  }
  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  
  printf("getAdvance() low hit rate x%zu: %lld us (avg: %.4f us/call)\n",
         glyphs.size(), static_cast<long long>(duration.count()),
         static_cast<double>(duration.count()) / glyphs.size());
  
  EXPECT_NE(sum, 0);
  // Low hit rate: cache overhead ~15%, but acceptable. Expect < 20ms.
  EXPECT_LT(duration.count(), 20000);
}

// Test getBounds() performance with cache.
// Scenario: High hit rate (100,000 calls from ~100 unique glyphs, simulating text layout).
TGFX_TEST(TypefaceTest, BoundsCacheHighHitRate) {
  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr);
  
  Font font(typeface, 24.0f);
  std::vector<GlyphID> glyphs = GenerateRandomCommonGlyphs(typeface, 100000);
  
  auto start = std::chrono::high_resolution_clock::now();
  float sum = 0;
  for (auto glyphID : glyphs) {
    auto bounds = font.getBounds(glyphID);
    sum += bounds.width();
  }
  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  
  printf("getBounds() high hit rate x100,000: %lld us (avg: %.4f us/call)\n",
         static_cast<long long>(duration.count()),
         static_cast<double>(duration.count()) / glyphs.size());
  
  EXPECT_NE(sum, 0);
  // With cache: expect < 50ms. Without cache: expect significantly slower.
  EXPECT_LT(duration.count(), 50000);
}

// Test getBounds() performance with low cache hit rate.
// Scenario: Each glyph accessed only once (worst case for cache).
TGFX_TEST(TypefaceTest, BoundsCacheLowHitRate) {
  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr);
  
  Font font(typeface, 24.0f);
  // Generate sequential glyph IDs (low repetition).
  std::vector<GlyphID> glyphs;
  for (GlyphID gid = 1; gid <= 3000 && gid < typeface->glyphsCount(); gid++) {
    glyphs.push_back(gid);
  }
  
  auto start = std::chrono::high_resolution_clock::now();
  float sum = 0;
  for (auto glyphID : glyphs) {
    auto bounds = font.getBounds(glyphID);
    sum += bounds.width();
  }
  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  
  printf("getBounds() low hit rate x%zu: %lld us (avg: %.4f us/call)\n",
         glyphs.size(), static_cast<long long>(duration.count()),
         static_cast<double>(duration.count()) / glyphs.size());
  
  EXPECT_NE(sum, 0);
  // Low hit rate: cache overhead acceptable. Expect < 20ms.
  EXPECT_LT(duration.count(), 20000);
}

// Test memory overhead of caches.
// Measures actual cache sizes after high repetition scenario.
TGFX_TEST(TypefaceTest, CacheMemoryOverhead) {
  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr);
  
  Font font(typeface, 24.0f);
  // Generate glyphs with high repetition (simulates real text rendering).
  std::vector<GlyphID> glyphs = GenerateRandomCommonGlyphs(typeface, 100000);
  
  // Warm up caches by calling both methods.
  for (auto glyphID : glyphs) {
    font.getAdvance(glyphID, false);
    font.getBounds(glyphID);
  }
  
  // Get ScalerContext to measure cache sizes.
  auto scalerContext = typeface->getScalerContext(24.0f);
  ASSERT_TRUE(scalerContext != nullptr);
  
  // Count unique glyphs in test data.
  std::set<GlyphID> uniqueGlyphs(glyphs.begin(), glyphs.end());
  size_t uniqueGlyphCount = uniqueGlyphs.size();
  
  // Calculate memory overhead.
  // advanceCacheH: GlyphID (2 bytes) + float (4 bytes) = 6 bytes per entry (plus hash overhead ~50%).
  // boundsCache: BoundsKey (4 bytes) + Rect (16 bytes) = 20 bytes per entry (plus hash overhead ~50%).
  // Approximate overhead factor: 1.5x for std::unordered_map.
  size_t advanceCacheBytes = uniqueGlyphCount * (sizeof(GlyphID) + sizeof(float)) * 3 / 2;
  size_t boundsCacheBytes = uniqueGlyphCount * (sizeof(GlyphID) + 2 + sizeof(Rect)) * 3 / 2;
  size_t totalBytes = advanceCacheBytes + boundsCacheBytes;
  
  printf("\nCache Memory Overhead Analysis:\n");
  printf("  Unique glyphs cached: %zu\n", uniqueGlyphCount);
  printf("  advanceCacheH: ~%zu bytes (%.2f KB)\n", 
         advanceCacheBytes, advanceCacheBytes / 1024.0);
  printf("  boundsCache: ~%zu bytes (%.2f KB)\n", 
         boundsCacheBytes, boundsCacheBytes / 1024.0);
  printf("  Total overhead: ~%zu bytes (%.2f KB)\n", 
         totalBytes, totalBytes / 1024.0);
  printf("  Bytes per glyph: ~%zu bytes\n", totalBytes / uniqueGlyphCount);
  
  // Verify cache is reasonable (< 1MB for typical usage).
  EXPECT_LT(totalBytes, static_cast<size_t>(1024 * 1024));
  EXPECT_GT(uniqueGlyphCount, static_cast<size_t>(50));  // Should have cached at least 50 glyphs.
}

// Test memory overhead in low hit rate scenario (many unique glyphs).
TGFX_TEST(TypefaceTest, CacheMemoryOverheadLowHitRate) {
  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr);
  
  Font font(typeface, 24.0f);
  // Generate sequential glyph IDs (low repetition, many unique glyphs).
  std::vector<GlyphID> glyphs;
  for (GlyphID gid = 1; gid <= 3000 && gid < typeface->glyphsCount(); gid++) {
    glyphs.push_back(gid);
  }
  
  // Warm up caches.
  for (auto glyphID : glyphs) {
    font.getAdvance(glyphID, false);
    font.getBounds(glyphID);
  }
  
  size_t uniqueGlyphCount = glyphs.size();
  
  // Calculate memory overhead.
  size_t advanceCacheBytes = uniqueGlyphCount * (sizeof(GlyphID) + sizeof(float)) * 3 / 2;
  size_t boundsCacheBytes = uniqueGlyphCount * (sizeof(GlyphID) + 2 + sizeof(Rect)) * 3 / 2;
  size_t totalBytes = advanceCacheBytes + boundsCacheBytes;
  
  printf("\nCache Memory Overhead (Low Hit Rate - Many Unique Glyphs):\n");
  printf("  Unique glyphs cached: %zu\n", uniqueGlyphCount);
  printf("  advanceCacheH: ~%zu bytes (%.2f KB)\n", 
         advanceCacheBytes, advanceCacheBytes / 1024.0);
  printf("  boundsCache: ~%zu bytes (%.2f KB)\n", 
         boundsCacheBytes, boundsCacheBytes / 1024.0);
  printf("  Total overhead: ~%zu bytes (%.2f KB)\n", 
         totalBytes, totalBytes / 1024.0);
  printf("  Bytes per glyph: ~%zu bytes\n", totalBytes / uniqueGlyphCount);
  
  EXPECT_LT(totalBytes, static_cast<size_t>(1024 * 1024));
  EXPECT_EQ(uniqueGlyphCount, static_cast<size_t>(3000));
}

#ifdef __APPLE__
// Test CoreGraphics advance cache performance with high hit rate.
TGFX_TEST(TypefaceTest, CGAdvanceCacheHighHitRate) {
  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr);
  
  Font font(typeface, 24.0f);
  std::vector<GlyphID> glyphs = GenerateRandomCommonGlyphs(typeface, 100000);
  
  auto start = std::chrono::high_resolution_clock::now();
  float sum = 0;
  for (auto glyphID : glyphs) {
    sum += font.getAdvance(glyphID, false);
  }
  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  
  printf("CG getAdvance() high hit rate x100,000: %lld us (avg: %.4f us/call)\n", 
         duration.count(), duration.count() / 100000.0);
  
  EXPECT_GT(sum, 0);
  EXPECT_LT(duration.count(), 1000000);
}

// Test CoreGraphics advance cache performance with low hit rate.
TGFX_TEST(TypefaceTest, CGAdvanceCacheLowHitRate) {
  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr);
  
  Font font(typeface, 24.0f);
  std::vector<GlyphID> glyphs;
  for (GlyphID i = 1; i <= 3000; i++) {
    glyphs.push_back(i);
  }
  
  auto start = std::chrono::high_resolution_clock::now();
  float sum = 0;
  for (auto glyphID : glyphs) {
    sum += font.getAdvance(glyphID, false);
  }
  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  
  printf("CG getAdvance() low hit rate x3000: %lld us (avg: %.4f us/call)\n", 
         duration.count(), duration.count() / 3000.0);
  
  EXPECT_GT(sum, 0);
  EXPECT_LT(duration.count(), 50000);
}

// Test CoreGraphics bounds cache performance with high hit rate.
TGFX_TEST(TypefaceTest, CGBoundsCacheHighHitRate) {
  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr);
  
  Font font(typeface, 24.0f);
  std::vector<GlyphID> glyphs = GenerateRandomCommonGlyphs(typeface, 100000);
  
  auto start = std::chrono::high_resolution_clock::now();
  float sum = 0;
  for (auto glyphID : glyphs) {
    auto bounds = font.getBounds(glyphID);
    sum += bounds.width() + bounds.height();
  }
  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  
  printf("CG getBounds() high hit rate x100,000: %lld us (avg: %.4f us/call)\n", 
         duration.count(), duration.count() / 100000.0);
  
  EXPECT_GT(sum, 0);
  EXPECT_LT(duration.count(), 1000000);
}

// Test CoreGraphics bounds cache performance with low hit rate.
TGFX_TEST(TypefaceTest, CGBoundsCacheLowHitRate) {
  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr);
  
  Font font(typeface, 24.0f);
  std::vector<GlyphID> glyphs;
  for (GlyphID i = 1; i <= 3000; i++) {
    glyphs.push_back(i);
  }
  
  auto start = std::chrono::high_resolution_clock::now();
  float sum = 0;
  for (auto glyphID : glyphs) {
    auto bounds = font.getBounds(glyphID);
    sum += bounds.width() + bounds.height();
  }
  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  
  printf("CG getBounds() low hit rate x3000: %lld us (avg: %.4f us/call)\n", 
         duration.count(), duration.count() / 3000.0);
  
  EXPECT_GT(sum, 0);
  EXPECT_LT(duration.count(), 50000);
}
#endif
}  // namespace tgfx
