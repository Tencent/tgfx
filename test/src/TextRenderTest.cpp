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

#include <cmath>
#include "gtest/gtest.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/RSXform.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/core/Surface.h"
#include "tgfx/core/TextBlob.h"
#include "tgfx/core/TextBlobBuilder.h"
#include "tgfx/core/UTF.h"
#include "utils/TestUtils.h"
#include "utils/TextShaper.h"

namespace tgfx {

TGFX_TEST(TextRenderTest, textShape) {
  auto serifTypeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));
  ASSERT_TRUE(serifTypeface != nullptr);
  std::string text =
      "ffi fl\n"
      "x²-y²\n"
      "🤡👨🏼‍🦱👨‍👨‍👧‍👦\n"
      "🇨🇳🇫🇮\n"
      "#️⃣#*️⃣*\n"
      "1️⃣🔟";
  auto positionedGlyphs = TextShaper::Shape(text, serifTypeface);

  float fontSize = 25.f;
  float lineHeight = fontSize * 1.2f;
  float height = 0;
  float width = 0;
  float x;
  struct TextRun {
    std::vector<GlyphID> ids;
    std::vector<Point> positions;
    Font font;
  };
  std::vector<TextRun> textRuns;
  Path path;
  TextRun* run = nullptr;
  auto count = positionedGlyphs.glyphCount();
  auto newline = [&]() {
    x = 0;
    height += lineHeight;
    path.moveTo({0, height});
  };
  newline();
  for (size_t i = 0; i < count; ++i) {
    auto typeface = positionedGlyphs.getTypeface(i);
    if (run == nullptr || run->font.getTypeface() != typeface) {
      run = &textRuns.emplace_back();
      run->font = Font(typeface, fontSize);
    }
    auto index = positionedGlyphs.getStringIndex(i);
    auto length = (i + 1 == count ? text.length() : positionedGlyphs.getStringIndex(i + 1)) - index;
    auto name = text.substr(index, length);
    if (name == "\n") {
      newline();
      continue;
    }
    auto glyphID = positionedGlyphs.getGlyphID(i);
    run->ids.emplace_back(glyphID);
    run->positions.push_back(Point{x, height});
    x += run->font.getAdvance(glyphID);
    path.lineTo({x, height});
    if (width < x) {
      width = x;
    }
  }
  height += lineHeight;

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface =
      Surface::Make(context, static_cast<int>(ceil(width)), static_cast<int>(ceil(height)));
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  Paint strokePaint;
  strokePaint.setColor(Color{1.f, 0.f, 0.f, 1.f});
  strokePaint.setStrokeWidth(2.f);
  strokePaint.setStyle(PaintStyle::Stroke);
  canvas->drawPath(path, strokePaint);

  Paint paint;
  paint.setColor(Color::Black());
  for (const auto& textRun : textRuns) {
    canvas->drawGlyphs(textRun.ids.data(), textRun.positions.data(), textRun.ids.size(),
                       textRun.font, paint);
  }
  context->flushAndSubmit();
  EXPECT_TRUE(Baseline::Compare(surface, "TextRenderTest/text_shape"));
}

TGFX_TEST(TextRenderTest, textEmojiMixedBlendModes1) {
  auto serifTypeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));
  ASSERT_TRUE(serifTypeface != nullptr);

  std::string mixedText = "Hello TGFX! 🎨🎉😊🌟✨🚀💻❤️";

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  int surfaceWidth = 1200;
  int surfaceHeight = 800;
  auto surface = Surface::Make(context, surfaceWidth, surfaceHeight);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();

  // Create gradient background
  canvas->clear(Color::White());
  auto backgroundPaint = Paint();
  auto colors = {Color::FromRGBA(255, 200, 200, 255), Color::FromRGBA(200, 200, 255, 255)};
  auto positions = {0.0f, 1.0f};
  auto shader = Shader::MakeLinearGradient(
      Point::Make(0, 0), Point::Make(surfaceWidth, surfaceHeight), colors, positions);
  backgroundPaint.setShader(shader);
  canvas->drawRect(Rect::MakeWH(surfaceWidth, surfaceHeight), backgroundPaint);

  float fontSize = 32.f;
  float lineHeight = fontSize * 1.5f;
  float startY = 60.f;

  // Test different blend modes
  std::vector<BlendMode> blendModes = {
      BlendMode::SrcOver,   BlendMode::SrcIn,     BlendMode::Src,        BlendMode::Overlay,
      BlendMode::Darken,    BlendMode::Lighten,   BlendMode::ColorDodge, BlendMode::ColorBurn,
      BlendMode::HardLight, BlendMode::SoftLight, BlendMode::Difference, BlendMode::Exclusion};

  std::vector<std::string> blendModeNames = {"SrcOver",   "Multiply",  "Screen",     "Overlay",
                                             "Darken",    "Lighten",   "ColorDodge", "ColorBurn",
                                             "HardLight", "SoftLight", "Difference", "Exclusion"};

  for (size_t modeIndex = 0; modeIndex < blendModes.size(); ++modeIndex) {
    auto blendMode = blendModes[modeIndex];
    auto modeName = blendModeNames[modeIndex];

    float y = startY + modeIndex * lineHeight;
    float x = 20.f;

    // Draw blend mode label
    Paint labelPaint;
    labelPaint.setColor(Color::Black());
    auto labelFont = Font(serifTypeface, 16.f);
    canvas->drawSimpleText(modeName, x, y - 8, labelFont, labelPaint);

    // Process text using TextShaper for proper emoji handling
    auto positionedGlyphs = TextShaper::Shape(mixedText, serifTypeface);

    struct TextRun {
      std::vector<GlyphID> ids;
      std::vector<Point> positions;
      Font font;
    };
    std::vector<TextRun> textRuns;
    TextRun* run = nullptr;
    auto count = positionedGlyphs.glyphCount();
    float textX = x + 120;

    for (size_t i = 0; i < count; ++i) {
      auto typeface = positionedGlyphs.getTypeface(i);
      if (run == nullptr || run->font.getTypeface() != typeface) {
        textRuns.emplace_back();
        run = &textRuns.back();
        run->font = Font(typeface, fontSize);
      }
      auto glyphID = positionedGlyphs.getGlyphID(i);
      run->ids.emplace_back(glyphID);
      run->positions.push_back(Point{textX, y});
      textX += run->font.getAdvance(glyphID);
    }

    // Draw mixed text with current blend mode using proper glyph rendering
    Paint textPaint;
    textPaint.setColor(Color::FromRGBA(255, 100, 50, 200));
    textPaint.setBlendMode(blendMode);

    for (const auto& textRun : textRuns) {
      canvas->drawGlyphs(textRun.ids.data(), textRun.positions.data(), textRun.ids.size(),
                         textRun.font, textPaint);
    }
  }

  context->flushAndSubmit();
  EXPECT_TRUE(Baseline::Compare(surface, "TextRenderTest/textEmojiMixedBlendModes"));
}

TGFX_TEST(TextRenderTest, textEmojiMixedBlendModes2) {
  auto serifTypeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));
  ASSERT_TRUE(serifTypeface != nullptr);

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  int surfaceWidth = 600;
  int surfaceHeight = 400;
  auto surface = Surface::Make(context, surfaceWidth, surfaceHeight);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();

  // Create colorful background with circles
  canvas->clear(Color::FromRGBA(240, 240, 255, 255));

  // Test emoji and text with different blend modes in layers
  std::vector<std::pair<std::string, BlendMode>> textBlendPairs = {{"🎨Art", BlendMode::SrcOver},
                                                                   {"🎨Art", BlendMode::SrcIn},
                                                                   {"🎭Mix", BlendMode::Src},
                                                                   {"🚀Fast", BlendMode::SrcATop},
                                                                   {"🎪Fun", BlendMode::SrcOut}};

  float fontSize = 36.f;

  for (size_t i = 0; i < textBlendPairs.size(); ++i) {
    auto& pair = textBlendPairs[i];
    auto& text = pair.first;
    auto blendMode = pair.second;

    float x = 50 + (i % 3) * 180;
    float y = 120 + (i / 3) * 120;

    // Process text using TextShaper for proper emoji handling
    auto positionedGlyphs = TextShaper::Shape(text, serifTypeface);

    struct TextRun {
      std::vector<GlyphID> ids;
      std::vector<Point> positions;
      Font font;
    };
    std::vector<TextRun> textRuns;
    TextRun* run = nullptr;
    auto count = positionedGlyphs.glyphCount();
    float textX = x;

    for (size_t j = 0; j < count; ++j) {
      auto typeface = positionedGlyphs.getTypeface(j);
      if (run == nullptr || run->font.getTypeface() != typeface) {
        textRuns.emplace_back();
        run = &textRuns.back();
        run->font = Font(typeface, fontSize);
      }
      auto glyphID = positionedGlyphs.getGlyphID(j);
      run->ids.emplace_back(glyphID);
      run->positions.push_back(Point{textX, y});
      textX += run->font.getAdvance(glyphID);
    }

    Paint textPaint;
    textPaint.setColor(Color::FromRGBA(255, 50, 100, 220));
    textPaint.setBlendMode(blendMode);

    for (const auto& textRun : textRuns) {
      canvas->drawGlyphs(textRun.ids.data(), textRun.positions.data(), textRun.ids.size(),
                         textRun.font, textPaint);
    }
  }

  context->flushAndSubmit();
  EXPECT_TRUE(Baseline::Compare(surface, "TextRenderTest/textEmojiMixedBlendModes2"));
}

TGFX_TEST(TextRenderTest, complexEmojiTextBlending) {
  auto serifTypeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));
  ASSERT_TRUE(serifTypeface != nullptr);

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  int surfaceWidth = 800;
  int surfaceHeight = 600;
  auto surface = Surface::Make(context, surfaceWidth, surfaceHeight);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();

  // Create complex background pattern
  canvas->clear(Color::White());

  // Draw gradient rectangles as background
  for (int i = 0; i < 8; ++i) {
    for (int j = 0; j < 6; ++j) {
      Paint rectPaint;
      auto hue = (i * 45 + j * 30) % 360;
      // Convert HSL to RGB approximation
      auto r = static_cast<uint8_t>(128 + 100 * sin(hue * 3.14159f / 180.0f));
      auto g = static_cast<uint8_t>(128 + 100 * sin((hue + 120) * 3.14159f / 180.0f));
      auto b = static_cast<uint8_t>(128 + 100 * sin((hue + 240) * 3.14159f / 180.0f));
      auto color = Color::FromRGBA(r, g, b, 77);  // 0.3f * 255
      rectPaint.setColor(color);
      canvas->drawRect(Rect::MakeXYWH(i * 100, j * 100, 100, 100), rectPaint);
    }
  }

  // Complex text with various emoji sequences
  std::vector<std::string> complexTexts = {"👨‍👩‍👧‍👦Family测试",
                                           "🏳️‍🌈Flag🇨🇳China",
                                           "👨🏼‍🦱Hair👩🏾‍💻Code",
                                           "🤡🎭🎪🎨艺术Art",
                                           "🌍🌎🌏World世界",
                                           "🎵🎶🎼音乐Music"};

  std::vector<BlendMode> complexBlendModes = {BlendMode::Multiply,   BlendMode::Screen,
                                              BlendMode::Overlay,    BlendMode::SoftLight,
                                              BlendMode::Difference, BlendMode::ColorBurn};

  float fontSize = 28.f;

  for (size_t i = 0; i < complexTexts.size(); ++i) {
    auto& text = complexTexts[i];
    auto blendMode = complexBlendModes[i];

    float x = 20 + (i % 2) * 380;
    float y = 80 + (i / 2) * 100;

    // Process text using TextShaper for proper emoji handling
    auto positionedGlyphs = TextShaper::Shape(text, serifTypeface);

    struct TextRun {
      std::vector<GlyphID> ids;
      std::vector<Point> positions;
      Font font;
    };
    std::vector<TextRun> textRuns;
    TextRun* run = nullptr;
    auto count = positionedGlyphs.glyphCount();
    float textX = x;

    for (size_t j = 0; j < count; ++j) {
      auto typeface = positionedGlyphs.getTypeface(j);
      if (run == nullptr || run->font.getTypeface() != typeface) {
        textRuns.emplace_back();
        run = &textRuns.back();
        run->font = Font(typeface, fontSize);
      }
      auto glyphID = positionedGlyphs.getGlyphID(j);
      run->ids.emplace_back(glyphID);
      run->positions.push_back(Point{textX, y});
      textX += run->font.getAdvance(glyphID);
    }

    // Draw text with blend mode
    Paint textPaint;
    textPaint.setColor(Color::FromRGBA(40, 80, 160, 255));
    textPaint.setBlendMode(blendMode);

    for (const auto& textRun : textRuns) {
      canvas->drawGlyphs(textRun.ids.data(), textRun.positions.data(), textRun.ids.size(),
                         textRun.font, textPaint);
    }

    // Draw blend mode label
    Paint labelPaint;
    labelPaint.setColor(Color::Black());
    auto labelFont = Font(serifTypeface, 12.f);
    std::string label = "BlendMode: " + std::to_string(static_cast<int>(blendMode));
    canvas->drawSimpleText(label, x, y + 15, labelFont, labelPaint);
  }

  context->flushAndSubmit();
  EXPECT_TRUE(Baseline::Compare(surface, "TextRenderTest/complexEmojiTextBlending"));
}

TGFX_TEST(TextRenderTest, emojiTextStrokeBlending) {
  auto serifTypeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));
  ASSERT_TRUE(serifTypeface != nullptr);
  auto emojiTypeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoColorEmoji.ttf"));
  ASSERT_TRUE(emojiTypeface != nullptr);

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  int surfaceWidth = 700;
  int surfaceHeight = 500;
  auto surface = Surface::Make(context, surfaceWidth, surfaceHeight);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();

  // Rainbow gradient background
  canvas->clear(Color::Black());
  auto colors = {Color::FromRGBA(255, 0, 0, 255),   Color::FromRGBA(255, 127, 0, 255),
                 Color::FromRGBA(255, 255, 0, 255), Color::FromRGBA(0, 255, 0, 255),
                 Color::FromRGBA(0, 0, 255, 255),   Color::FromRGBA(75, 0, 130, 255),
                 Color::FromRGBA(148, 0, 211, 255)};
  auto positions = {0.0f, 0.16f, 0.33f, 0.5f, 0.66f, 0.83f, 1.0f};
  auto shader = Shader::MakeLinearGradient(Point::Make(0, 0), Point::Make(0, surfaceHeight), colors,
                                           positions);
  Paint bgPaint;
  bgPaint.setShader(shader);
  canvas->drawRect(Rect::MakeWH(surfaceWidth, surfaceHeight), bgPaint);

  // Test stroke and fill with different blend modes
  std::string emojiText = "🎨🌈🎭🎪🚀";
  std::string normalText = "ArtRainbowMask";

  float fontSize = 48.f;

  std::vector<BlendMode> strokeBlendModes = {BlendMode::SrcOver, BlendMode::Multiply,
                                             BlendMode::Screen, BlendMode::Overlay,
                                             BlendMode::Difference};

  for (size_t i = 0; i < strokeBlendModes.size(); ++i) {
    auto blendMode = strokeBlendModes[i];
    float y = 80 + i * 80;

    // Process emoji text using TextShaper
    auto emojiPositionedGlyphs = TextShaper::Shape(emojiText, emojiTypeface);
    struct TextRun {
      std::vector<GlyphID> ids;
      std::vector<Point> positions;
      Font font;
    };
    std::vector<TextRun> emojiTextRuns;
    TextRun* emojiRun = nullptr;
    auto emojiCount = emojiPositionedGlyphs.glyphCount();
    float emojiX = 50;

    for (size_t j = 0; j < emojiCount; ++j) {
      auto typeface = emojiPositionedGlyphs.getTypeface(j);
      if (emojiRun == nullptr || emojiRun->font.getTypeface() != typeface) {
        emojiTextRuns.emplace_back();
        emojiRun = &emojiTextRuns.back();
        emojiRun->font = Font(typeface, fontSize);
      }
      auto glyphID = emojiPositionedGlyphs.getGlyphID(j);
      emojiRun->ids.emplace_back(glyphID);
      emojiRun->positions.push_back(Point{emojiX, y});
      emojiX += emojiRun->font.getAdvance(glyphID);
    }

    Paint emojiPaint;
    emojiPaint.setBlendMode(blendMode);

    for (const auto& textRun : emojiTextRuns) {
      canvas->drawGlyphs(textRun.ids.data(), textRun.positions.data(), textRun.ids.size(),
                         textRun.font, emojiPaint);
    }

    // Process normal text using TextShaper
    auto normalPositionedGlyphs = TextShaper::Shape(normalText, serifTypeface);
    std::vector<TextRun> normalTextRuns;
    TextRun* normalRun = nullptr;
    auto normalCount = normalPositionedGlyphs.glyphCount();
    float normalX = 350;

    for (size_t j = 0; j < normalCount; ++j) {
      auto typeface = normalPositionedGlyphs.getTypeface(j);
      if (normalRun == nullptr || normalRun->font.getTypeface() != typeface) {
        normalTextRuns.emplace_back();
        normalRun = &normalTextRuns.back();
        normalRun->font = Font(typeface, fontSize);
      }
      auto glyphID = normalPositionedGlyphs.getGlyphID(j);
      normalRun->ids.emplace_back(glyphID);
      normalRun->positions.push_back(Point{normalX, y});
      normalX += normalRun->font.getAdvance(glyphID);
    }

    // Draw normal text for comparison
    Paint textStrokePaint;
    textStrokePaint.setColor(Color::Green());
    textStrokePaint.setStyle(PaintStyle::Stroke);
    textStrokePaint.setStrokeWidth(2.0f);
    textStrokePaint.setBlendMode(blendMode);

    Paint textFillPaint;
    textFillPaint.setColor(Color::FromRGBA(100, 150, 255, 200));
    textFillPaint.setBlendMode(blendMode);

    for (const auto& textRun : normalTextRuns) {
      canvas->drawGlyphs(textRun.ids.data(), textRun.positions.data(), textRun.ids.size(),
                         textRun.font, textStrokePaint);
      canvas->drawGlyphs(textRun.ids.data(), textRun.positions.data(), textRun.ids.size(),
                         textRun.font, textFillPaint);
    }
  }

  context->flushAndSubmit();
  EXPECT_TRUE(Baseline::Compare(surface, "TextRenderTest/emojiTextStrokeBlending"));
}

TGFX_TEST(TextRenderTest, textEmojiOverlayBlendModes) {
  auto serifTypeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));
  ASSERT_TRUE(serifTypeface != nullptr);
  auto emojiTypeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoColorEmoji.ttf"));
  ASSERT_TRUE(emojiTypeface != nullptr);

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  int surfaceWidth = 1200;
  int surfaceHeight = 900;
  auto surface = Surface::Make(context, surfaceWidth, surfaceHeight);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();

  // Create striped background
  canvas->clear(Color::FromRGBA(230, 230, 250, 255));
  Paint stripePaint;
  stripePaint.setColor(Color::FromRGBA(200, 220, 240, 255));
  for (int i = 0; i < surfaceHeight; i += 20) {
    if ((i / 20) % 2 == 0) {
      canvas->drawRect(Rect::MakeXYWH(0, i, surfaceWidth, 20), stripePaint);
    }
  }

  float fontSize = 36.f;
  float lineHeight = 80.f;
  float startY = 60.f;

  // Test different blend modes for emoji overlays on text
  std::vector<BlendMode> blendModes = {
      BlendMode::SrcOver,   BlendMode::SrcIn,     BlendMode::SrcOut,     BlendMode::SrcATop,
      BlendMode::DstOver,   BlendMode::DstIn,     BlendMode::DstOut,     BlendMode::DstATop,
      BlendMode::Xor,       BlendMode::Multiply,  BlendMode::Screen,     BlendMode::Overlay,
      BlendMode::Darken,    BlendMode::Lighten,   BlendMode::ColorDodge, BlendMode::ColorBurn,
      BlendMode::HardLight, BlendMode::SoftLight, BlendMode::Difference, BlendMode::Exclusion};

  std::vector<std::string> blendModeNames = {
      "SrcOver", "SrcIn",      "SrcOut",    "SrcATop",   "DstOver",   "DstIn",      "DstOut",
      "DstATop", "Xor",        "Plus",      "Multiply",  "Screen",    "Overlay",    "Darken",
      "Lighten", "ColorDodge", "ColorBurn", "HardLight", "SoftLight", "Difference", "Exclusion"};

  std::string baseText = "Hello 世界";
  std::string emojiText = "🎨🎉🌟";

  for (size_t modeIndex = 0; modeIndex < blendModes.size(); ++modeIndex) {
    auto blendMode = blendModes[modeIndex];
    auto modeName = blendModeNames[modeIndex];

    float y = startY + (modeIndex / 3) * lineHeight;
    float x = 50.f + (modeIndex % 3) * 380.f;

    // Draw blend mode label
    Paint labelPaint;
    labelPaint.setColor(Color::Black());
    auto labelFont = Font(serifTypeface, 14.f);
    canvas->drawSimpleText(modeName, x, y - 20, labelFont, labelPaint);

    // First draw base text layer
    auto basePositionedGlyphs = TextShaper::Shape(baseText, serifTypeface);

    struct TextRun {
      std::vector<GlyphID> ids;
      std::vector<Point> positions;
      Font font;
    };
    std::vector<TextRun> baseTextRuns;
    TextRun* baseRun = nullptr;
    auto baseCount = basePositionedGlyphs.glyphCount();
    float baseX = x;

    for (size_t i = 0; i < baseCount; ++i) {
      auto typeface = basePositionedGlyphs.getTypeface(i);
      if (baseRun == nullptr || baseRun->font.getTypeface() != typeface) {
        baseTextRuns.emplace_back();
        baseRun = &baseTextRuns.back();
        baseRun->font = Font(typeface, fontSize);
      }
      auto glyphID = basePositionedGlyphs.getGlyphID(i);
      baseRun->ids.emplace_back(glyphID);
      baseRun->positions.push_back(Point{baseX, y});
      baseX += baseRun->font.getAdvance(glyphID);
    }

    // Draw base text with semi-transparent blue
    Paint baseTextPaint;
    baseTextPaint.setColor(Color::FromRGBA(50, 100, 200, 180));
    baseTextPaint.setBlendMode(BlendMode::SrcOver);

    for (const auto& textRun : baseTextRuns) {
      canvas->drawGlyphs(textRun.ids.data(), textRun.positions.data(), textRun.ids.size(),
                         textRun.font, baseTextPaint);
    }

    // Then overlay emoji with different blend modes
    auto emojiPositionedGlyphs = TextShaper::Shape(emojiText, emojiTypeface);
    std::vector<TextRun> emojiTextRuns;
    TextRun* emojiRun = nullptr;
    auto emojiCount = emojiPositionedGlyphs.glyphCount();
    float emojiX = x + 20;

    for (size_t i = 0; i < emojiCount; ++i) {
      auto typeface = emojiPositionedGlyphs.getTypeface(i);
      if (emojiRun == nullptr || emojiRun->font.getTypeface() != typeface) {
        emojiTextRuns.emplace_back();
        emojiRun = &emojiTextRuns.back();
        emojiRun->font = Font(typeface, fontSize);
      }
      auto glyphID = emojiPositionedGlyphs.getGlyphID(i);
      emojiRun->ids.emplace_back(glyphID);
      emojiRun->positions.push_back(Point{emojiX, y + 5});
      emojiX += emojiRun->font.getAdvance(glyphID);
    }

    // Draw overlaid emoji with the current blend mode
    Paint emojiPaint;
    emojiPaint.setColor(Color::FromRGBA(255, 150, 50, 200));
    emojiPaint.setBlendMode(blendMode);

    for (const auto& textRun : emojiTextRuns) {
      canvas->drawGlyphs(textRun.ids.data(), textRun.positions.data(), textRun.ids.size(),
                         textRun.font, emojiPaint);
    }
  }

  context->flushAndSubmit();
  EXPECT_TRUE(Baseline::Compare(surface, "TextRenderTest/textEmojiOverlayBlendModes"));
}

TGFX_TEST(TextRenderTest, TextBlobHitTestPoint) {
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSansSC-Regular.otf"));
  ASSERT_TRUE(typeface != nullptr);
  auto emojiTypeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoColorEmoji.ttf"));
  ASSERT_TRUE(emojiTypeface != nullptr);

  Font font(typeface, 80.0f);
  Font emojiFont(emojiTypeface, 80.0f);
  ASSERT_TRUE(font.hasOutlines());
  ASSERT_FALSE(emojiFont.hasOutlines());

  // Create a TextBlob with both normal character "O" and emoji "😀"
  auto glyphID_O = font.getGlyphID('O');
  auto glyphID_emoji = emojiFont.getGlyphID(0x1F600);  // 😀
  ASSERT_TRUE(glyphID_O > 0);
  ASSERT_TRUE(glyphID_emoji > 0);

  float advance_O = font.getAdvance(glyphID_O);
  float emojiOffsetX = advance_O + 10.0f;

  TextBlobBuilder builder;
  GlyphID glyphs1[] = {glyphID_O};
  const auto& buffer1 = builder.allocRunPos(font, 1);
  buffer1.glyphs[0] = glyphs1[0];
  buffer1.positions[0] = 0.0f;
  buffer1.positions[1] = 0.0f;
  GlyphID glyphs2[] = {glyphID_emoji};
  const auto& buffer2 = builder.allocRunPos(emojiFont, 1);
  buffer2.glyphs[0] = glyphs2[0];
  buffer2.positions[0] = emojiOffsetX;
  buffer2.positions[1] = 0.0f;
  auto textBlob = builder.build();
  ASSERT_TRUE(textBlob != nullptr);

  // Get bounds for "O" character
  auto bounds_O = font.getBounds(glyphID_O);
  // Get bounds for emoji character (offset by position)
  auto bounds_emoji = emojiFont.getBounds(glyphID_emoji);
  bounds_emoji.offset(advance_O + 10.0f, 0);

  // ========== Test "O" character (outline-based hit test) ==========
  // Test 1: Hit on the outline of "O"
  float onOutlineX = bounds_O.left + 5.0f;
  float onOutlineY = bounds_O.centerY();
  EXPECT_TRUE(textBlob->hitTestPoint(onOutlineX, onOutlineY, nullptr));

  // Test 2: Miss in the center hole of "O"
  float centerX_O = bounds_O.centerX();
  float centerY_O = bounds_O.centerY();
  EXPECT_FALSE(textBlob->hitTestPoint(centerX_O, centerY_O, nullptr));

  // Test 3: Miss outside the "O" bounds
  float outsideX_O = bounds_O.left - 5.0f;
  float outsideY_O = bounds_O.centerY();
  EXPECT_FALSE(textBlob->hitTestPoint(outsideX_O, outsideY_O, nullptr));

  // Test 4: Hit outside "O" but within stroke area
  Stroke stroke = {};
  stroke.width = 20.0f;  // half = 10
  EXPECT_TRUE(textBlob->hitTestPoint(outsideX_O, outsideY_O, &stroke));

  // Test 5: Miss further outside "O", beyond stroke area
  float farOutsideX_O = bounds_O.left - 15.0f;
  EXPECT_FALSE(textBlob->hitTestPoint(farOutsideX_O, outsideY_O, &stroke));

  // ========== Test emoji character (bounds-based hit test) ==========
  // Test 6: Hit inside the emoji bounds
  float centerX_emoji = bounds_emoji.centerX();
  float centerY_emoji = bounds_emoji.centerY();
  EXPECT_TRUE(textBlob->hitTestPoint(centerX_emoji, centerY_emoji, nullptr));

  // Test 7: Hit on the edge of the emoji bounds
  float edgeX_emoji = bounds_emoji.left + 1.0f;
  float edgeY_emoji = bounds_emoji.centerY();
  EXPECT_TRUE(textBlob->hitTestPoint(edgeX_emoji, edgeY_emoji, nullptr));

  // Test 8: Miss outside the emoji bounds
  float outsideX_emoji = bounds_emoji.left - 5.0f;
  float outsideY_emoji = bounds_emoji.centerY();
  EXPECT_FALSE(textBlob->hitTestPoint(outsideX_emoji, outsideY_emoji, nullptr));

  // Test 9: Hit outside emoji but within stroke area
  EXPECT_TRUE(textBlob->hitTestPoint(outsideX_emoji, outsideY_emoji, &stroke));

  // Test 10: Miss further outside emoji, beyond stroke area
  // Use a point that's clearly outside both characters
  float farOutsideX_emoji = bounds_emoji.right + 20.0f;
  EXPECT_FALSE(textBlob->hitTestPoint(farOutsideX_emoji, outsideY_emoji, &stroke));
}

TGFX_TEST(TextRenderTest, TextBlobMakeFromText) {
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSansSC-Regular.otf"));
  ASSERT_TRUE(typeface != nullptr);
  Font font(typeface, 40.0f);

  // Test basic text creation
  auto blob = TextBlob::MakeFrom("Hello", font);
  ASSERT_TRUE(blob != nullptr);
  auto bounds = blob->getBounds();
  EXPECT_GT(bounds.width(), 0.0f);
  EXPECT_GT(bounds.height(), 0.0f);

  // Test empty text returns nullptr
  auto emptyBlob = TextBlob::MakeFrom("", font);
  EXPECT_TRUE(emptyBlob == nullptr);

  // Test text with unmapped characters only returns nullptr
  auto unmappedBlob = TextBlob::MakeFrom("\n\t", font);
  EXPECT_TRUE(unmappedBlob == nullptr);

  // Test mixed valid and invalid characters
  auto mixedBlob = TextBlob::MakeFrom("A\nB", font);
  ASSERT_TRUE(mixedBlob != nullptr);

  // Test Chinese text
  auto chineseBlob = TextBlob::MakeFrom("你好世界", font);
  ASSERT_TRUE(chineseBlob != nullptr);
  EXPECT_GT(chineseBlob->getBounds().width(), 0.0f);
}

TGFX_TEST(TextRenderTest, TextBlobMakeFromGlyphs) {
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSansSC-Regular.otf"));
  ASSERT_TRUE(typeface != nullptr);
  Font font(typeface, 40.0f);

  auto glyphA = font.getGlyphID('A');
  auto glyphB = font.getGlyphID('B');
  auto glyphC = font.getGlyphID('C');
  ASSERT_TRUE(glyphA > 0 && glyphB > 0 && glyphC > 0);

  // Test MakeFrom with Point positions
  GlyphID glyphs[] = {glyphA, glyphB, glyphC};
  Point positions[] = {{0, 0}, {50, 10}, {100, 20}};
  auto blob = TextBlob::MakeFrom(glyphs, positions, 3, font);
  ASSERT_TRUE(blob != nullptr);
  auto bounds = blob->getBounds();
  EXPECT_GT(bounds.width(), 0.0f);

  // Test zero glyphCount returns nullptr
  auto emptyBlob = TextBlob::MakeFrom(glyphs, positions, 0, font);
  EXPECT_TRUE(emptyBlob == nullptr);

  // Test nullptr typeface returns nullptr
  Font invalidFont(nullptr, 40.0f);
  auto invalidBlob = TextBlob::MakeFrom(glyphs, positions, 3, invalidFont);
  EXPECT_TRUE(invalidBlob == nullptr);
}

TGFX_TEST(TextRenderTest, TextBlobMakeFromPosH) {
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSansSC-Regular.otf"));
  ASSERT_TRUE(typeface != nullptr);
  Font font(typeface, 40.0f);

  auto glyphA = font.getGlyphID('A');
  auto glyphB = font.getGlyphID('B');
  auto glyphC = font.getGlyphID('C');
  ASSERT_TRUE(glyphA > 0 && glyphB > 0 && glyphC > 0);

  GlyphID glyphs[] = {glyphA, glyphB, glyphC};
  float xPositions[] = {0.0f, 40.0f, 80.0f};
  float y = 50.0f;

  auto blob = TextBlob::MakeFromPosH(glyphs, xPositions, 3, y, font);
  ASSERT_TRUE(blob != nullptr);
  auto bounds = blob->getBounds();
  EXPECT_GT(bounds.width(), 0.0f);

  // Test zero glyphCount returns nullptr
  auto emptyBlob = TextBlob::MakeFromPosH(glyphs, xPositions, 0, y, font);
  EXPECT_TRUE(emptyBlob == nullptr);
}

TGFX_TEST(TextRenderTest, TextBlobMakeFromRSXform) {
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSansSC-Regular.otf"));
  ASSERT_TRUE(typeface != nullptr);
  Font font(typeface, 40.0f);

  auto glyphA = font.getGlyphID('A');
  auto glyphB = font.getGlyphID('B');
  ASSERT_TRUE(glyphA > 0 && glyphB > 0);

  GlyphID glyphs[] = {glyphA, glyphB};
  // RSXform: scos, ssin, tx, ty per glyph
  RSXform xforms[] = {
      RSXform::Make(1.0f, 0.0f, 0.0f, 0.0f),    // First glyph: no rotation, at origin
      RSXform::Make(0.7f, 0.7f, 50.0f, 20.0f),  // Second glyph: 45 degree rotation, translated
  };

  auto blob = TextBlob::MakeFromRSXform(glyphs, xforms, 2, font);
  ASSERT_TRUE(blob != nullptr);
  auto bounds = blob->getBounds();
  EXPECT_GT(bounds.width(), 0.0f);

  // Test zero glyphCount returns nullptr
  auto emptyBlob = TextBlob::MakeFromRSXform(glyphs, xforms, 0, font);
  EXPECT_TRUE(emptyBlob == nullptr);
}

TGFX_TEST(TextRenderTest, TextBlobBuilderAllocRun) {
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSansSC-Regular.otf"));
  ASSERT_TRUE(typeface != nullptr);
  Font font(typeface, 40.0f);

  auto glyphA = font.getGlyphID('A');
  auto glyphB = font.getGlyphID('B');
  auto glyphC = font.getGlyphID('C');
  ASSERT_TRUE(glyphA > 0 && glyphB > 0 && glyphC > 0);

  TextBlobBuilder builder;
  const auto& buffer = builder.allocRunPosH(font, 3, 50.0f);
  EXPECT_TRUE(buffer.glyphs != nullptr);
  EXPECT_TRUE(buffer.positions != nullptr);
  buffer.glyphs[0] = glyphA;
  buffer.glyphs[1] = glyphB;
  buffer.glyphs[2] = glyphC;
  float x = 10.0f;
  buffer.positions[0] = x;
  x += font.getAdvance(glyphA);
  buffer.positions[1] = x;
  x += font.getAdvance(glyphB);
  buffer.positions[2] = x;

  auto blob = builder.build();
  ASSERT_TRUE(blob != nullptr);
  auto bounds = blob->getBounds();
  EXPECT_GT(bounds.width(), 0.0f);
}

TGFX_TEST(TextRenderTest, TextBlobBuilderMultipleRuns) {
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSansSC-Regular.otf"));
  ASSERT_TRUE(typeface != nullptr);
  auto emojiTypeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoColorEmoji.ttf"));
  ASSERT_TRUE(emojiTypeface != nullptr);

  Font font(typeface, 40.0f);
  Font emojiFont(emojiTypeface, 40.0f);

  auto glyphA = font.getGlyphID('A');
  auto glyphB = font.getGlyphID('B');
  auto glyphEmoji = emojiFont.getGlyphID(0x1F600);
  ASSERT_TRUE(glyphA > 0 && glyphB > 0 && glyphEmoji > 0);

  TextBlobBuilder builder;

  // Run 1: Point positioning
  const auto& buffer1 = builder.allocRunPos(font, 2);
  buffer1.glyphs[0] = glyphA;
  buffer1.glyphs[1] = glyphB;
  buffer1.positions[0] = 0.0f;
  buffer1.positions[1] = 0.0f;
  buffer1.positions[2] = 40.0f;
  buffer1.positions[3] = 0.0f;

  // Run 2: Horizontal positioning with emoji
  const auto& buffer2 = builder.allocRunPosH(emojiFont, 1, 0.0f);
  buffer2.glyphs[0] = glyphEmoji;
  buffer2.positions[0] = 80.0f;

  auto blob = builder.build();
  ASSERT_TRUE(blob != nullptr);
  auto bounds = blob->getBounds();
  EXPECT_GT(bounds.width(), 0.0f);
}

TGFX_TEST(TextRenderTest, TextBlobBuilderSetBounds) {
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSansSC-Regular.otf"));
  ASSERT_TRUE(typeface != nullptr);
  Font font(typeface, 40.0f);

  auto glyphA = font.getGlyphID('A');
  ASSERT_TRUE(glyphA > 0);

  TextBlobBuilder builder;
  const auto& buffer = builder.allocRunPos(font, 1);
  buffer.glyphs[0] = glyphA;
  buffer.positions[0] = 10.0f;
  buffer.positions[1] = 20.0f;

  // Set custom bounds
  Rect customBounds = Rect::MakeXYWH(0, 0, 200, 100);
  builder.setBounds(customBounds);

  auto blob = builder.build();
  ASSERT_TRUE(blob != nullptr);
  auto bounds = blob->getBounds();
  EXPECT_FLOAT_EQ(bounds.left, customBounds.left);
  EXPECT_FLOAT_EQ(bounds.top, customBounds.top);
  EXPECT_FLOAT_EQ(bounds.right, customBounds.right);
  EXPECT_FLOAT_EQ(bounds.bottom, customBounds.bottom);
}

TGFX_TEST(TextRenderTest, TextBlobBuilderEmptyBuild) {
  TextBlobBuilder builder;
  auto blob = builder.build();
  EXPECT_TRUE(blob == nullptr);
}

TGFX_TEST(TextRenderTest, TextBlobBuilderReuse) {
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSansSC-Regular.otf"));
  ASSERT_TRUE(typeface != nullptr);
  Font font(typeface, 40.0f);

  auto glyphA = font.getGlyphID('A');
  auto glyphB = font.getGlyphID('B');
  ASSERT_TRUE(glyphA > 0 && glyphB > 0);

  TextBlobBuilder builder;

  // First build
  const auto& buffer1 = builder.allocRunPos(font, 1);
  buffer1.glyphs[0] = glyphA;
  buffer1.positions[0] = 0.0f;
  buffer1.positions[1] = 0.0f;
  auto blob1 = builder.build();
  ASSERT_TRUE(blob1 != nullptr);

  // Second build with same builder
  const auto& buffer2 = builder.allocRunPos(font, 1);
  buffer2.glyphs[0] = glyphB;
  buffer2.positions[0] = 0.0f;
  buffer2.positions[1] = 0.0f;
  auto blob2 = builder.build();
  ASSERT_TRUE(blob2 != nullptr);

  // Blobs should be independent
  EXPECT_NE(blob1.get(), blob2.get());
}

TGFX_TEST(TextRenderTest, TextBlobGetTightBounds) {
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSansSC-Regular.otf"));
  ASSERT_TRUE(typeface != nullptr);
  Font font(typeface, 40.0f);

  auto blob = TextBlob::MakeFrom("ABC", font);
  ASSERT_TRUE(blob != nullptr);

  // Test without matrix
  auto tightBounds = blob->getTightBounds();
  EXPECT_GT(tightBounds.width(), 0.0f);
  EXPECT_GT(tightBounds.height(), 0.0f);

  // Test with scale matrix
  Matrix scaleMatrix = Matrix::MakeScale(2.0f);
  auto scaledBounds = blob->getTightBounds(&scaleMatrix);
  EXPECT_GT(scaledBounds.width(), tightBounds.width());

  // Test with zero scale matrix returns empty
  Matrix zeroMatrix = Matrix::MakeScale(0.0f);
  auto zeroBounds = blob->getTightBounds(&zeroMatrix);
  EXPECT_TRUE(zeroBounds.isEmpty());
}

TGFX_TEST(TextRenderTest, TextBlobPositioningRender) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 388, 412);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSansSC-Regular.otf"));
  ASSERT_TRUE(typeface != nullptr);
  Font font(typeface, 30.0f);

  auto glyphA = font.getGlyphID('A');
  auto glyphB = font.getGlyphID('B');
  auto glyphC = font.getGlyphID('C');
  auto glyphD = font.getGlyphID('D');
  ASSERT_TRUE(glyphA > 0 && glyphB > 0 && glyphC > 0 && glyphD > 0);

  Paint paint;
  paint.setColor(Color::Black());

  Paint labelPaint;
  labelPaint.setColor(Color::FromRGBA(100, 100, 100, 255));
  Font labelFont(typeface, 14.0f);

  float y = 72.0f;
  float spacing = 70.0f;

  // 1. Default horizontal text (using default advances)
  canvas->drawSimpleText("Default:", 49.0f, y - 10, labelFont, labelPaint);
  {
    TextBlobBuilder builder;
    const auto& buffer = builder.allocRunPosH(font, 4, y);
    buffer.glyphs[0] = glyphA;
    buffer.glyphs[1] = glyphB;
    buffer.glyphs[2] = glyphC;
    buffer.glyphs[3] = glyphD;
    float x = 139.0f;
    buffer.positions[0] = x;
    x += font.getAdvance(glyphA);
    buffer.positions[1] = x;
    x += font.getAdvance(glyphB);
    buffer.positions[2] = x;
    x += font.getAdvance(glyphC);
    buffer.positions[3] = x;
    auto blob = builder.build();
    canvas->drawTextBlob(blob, 0, 0, paint);
  }
  y += spacing;

  // 2. Horizontal text with custom spacing
  canvas->drawSimpleText("Horizontal:", 49.0f, y - 10, labelFont, labelPaint);
  {
    TextBlobBuilder builder;
    const auto& buffer = builder.allocRunPosH(font, 4, y);
    buffer.glyphs[0] = glyphA;
    buffer.glyphs[1] = glyphB;
    buffer.glyphs[2] = glyphC;
    buffer.glyphs[3] = glyphD;
    buffer.positions[0] = 139.0f;
    buffer.positions[1] = 179.0f;
    buffer.positions[2] = 239.0f;
    buffer.positions[3] = 319.0f;
    auto blob = builder.build();
    canvas->drawTextBlob(blob, 0, 0, paint);
  }
  y += spacing;

  // 3. Point positioning (allocRunPos)
  canvas->drawSimpleText("Point:", 49.0f, y - 10, labelFont, labelPaint);
  {
    TextBlobBuilder builder;
    const auto& buffer = builder.allocRunPos(font, 4);
    buffer.glyphs[0] = glyphA;
    buffer.glyphs[1] = glyphB;
    buffer.glyphs[2] = glyphC;
    buffer.glyphs[3] = glyphD;
    buffer.positions[0] = 139.0f;
    buffer.positions[1] = y;
    buffer.positions[2] = 189.0f;
    buffer.positions[3] = y - 15.0f;
    buffer.positions[4] = 239.0f;
    buffer.positions[5] = y + 15.0f;
    buffer.positions[6] = 289.0f;
    buffer.positions[7] = y;
    auto blob = builder.build();
    canvas->drawTextBlob(blob, 0, 0, paint);
  }
  y += spacing;

  // 4. RSXform positioning (allocRunRSXform) - rotate around glyph center
  canvas->drawSimpleText("RSXform:", 49.0f, y - 10, labelFont, labelPaint);
  {
    GlyphID glyphs[] = {glyphA, glyphB, glyphC, glyphD};
    float angles[] = {0.0f, 30.0f, 60.0f, 90.0f};
    float xPositions[] = {139.0f, 189.0f, 239.0f, 289.0f};

    TextBlobBuilder builder;
    const auto& buffer = builder.allocRunRSXform(font, 4);
    for (int i = 0; i < 4; i++) {
      buffer.glyphs[i] = glyphs[i];
      auto bounds = font.getBounds(glyphs[i]);
      float cx = bounds.centerX();
      float cy = bounds.centerY();
      float rad = angles[i] * static_cast<float>(M_PI) / 180.0f;
      float scos = cosf(rad);
      float ssin = sinf(rad);
      // tx, ty: position of origin after transform, adjusted for center rotation
      // To rotate around center: tx = targetX + cx - (scos*cx - ssin*cy)
      //                          ty = targetY + cy - (ssin*cx + scos*cy)
      // But we want the center to be at (targetX + cx, y + cy), so:
      float targetCenterX = xPositions[i] + cx;
      float targetCenterY = y + cy;
      float tx = targetCenterX - (scos * cx - ssin * cy);
      float ty = targetCenterY - (ssin * cx + scos * cy);
      reinterpret_cast<RSXform*>(buffer.positions)[i] = RSXform::Make(scos, ssin, tx, ty);
    }
    auto blob = builder.build();
    canvas->drawTextBlob(blob, 0, 0, paint);
  }
  y += spacing;

  // 5. Matrix positioning (allocRunMatrix)
  canvas->drawSimpleText("Matrix:", 49.0f, y - 10, labelFont, labelPaint);
  {
    TextBlobBuilder builder;
    const auto& buffer = builder.allocRunMatrix(font, 4);
    buffer.glyphs[0] = glyphA;
    buffer.glyphs[1] = glyphB;
    buffer.glyphs[2] = glyphC;
    buffer.glyphs[3] = glyphD;
    // scaleX, skewX, transX, skewY, scaleY, transY for each glyph
    // Glyph A: normal
    buffer.positions[0] = 1.0f;
    buffer.positions[1] = 0.0f;
    buffer.positions[2] = 139.0f;
    buffer.positions[3] = 0.0f;
    buffer.positions[4] = 1.0f;
    buffer.positions[5] = y;
    // Glyph B: scaled 1.5x
    buffer.positions[6] = 1.5f;
    buffer.positions[7] = 0.0f;
    buffer.positions[8] = 189.0f;
    buffer.positions[9] = 0.0f;
    buffer.positions[10] = 1.5f;
    buffer.positions[11] = y;
    // Glyph C: skewed
    buffer.positions[12] = 1.0f;
    buffer.positions[13] = 0.3f;
    buffer.positions[14] = 249.0f;
    buffer.positions[15] = 0.0f;
    buffer.positions[16] = 1.0f;
    buffer.positions[17] = y;
    // Glyph D: rotated 45 degrees around center
    {
      auto bounds = font.getBounds(glyphD);
      float cx = bounds.centerX();
      float cy = bounds.centerY();
      float angle = 45.0f * static_cast<float>(M_PI) / 180.0f;
      float cosA = cosf(angle);
      float sinA = sinf(angle);
      // M = T(targetX + cx, y + cy) * R(angle) * T(-cx, -cy)
      // scaleX = cosA, skewX = -sinA, transX = targetX + cx - cosA*cx + sinA*cy
      // skewY = sinA, scaleY = cosA, transY = y + cy - sinA*cx - cosA*cy
      float targetX = 309.0f;
      buffer.positions[18] = cosA;
      buffer.positions[19] = -sinA;
      buffer.positions[20] = targetX + cx - cosA * cx + sinA * cy;
      buffer.positions[21] = sinA;
      buffer.positions[22] = cosA;
      buffer.positions[23] = y + cy - sinA * cx - cosA * cy;
    }
    auto blob = builder.build();
    canvas->drawTextBlob(blob, 0, 0, paint);
  }

  context->flushAndSubmit();
  EXPECT_TRUE(Baseline::Compare(surface, "TextRenderTest/TextBlobPositioningRender"));
}

TGFX_TEST(TextRenderTest, TextBlobMixedPositioning) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 285, 139);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSansSC-Regular.otf"));
  ASSERT_TRUE(typeface != nullptr);
  auto emojiTypeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoColorEmoji.ttf"));
  ASSERT_TRUE(emojiTypeface != nullptr);

  Font font(typeface, 36.0f);
  Font emojiFont(emojiTypeface, 36.0f);

  auto glyphH = font.getGlyphID('H');
  auto glyphI = font.getGlyphID('i');
  auto glyphEmoji = emojiFont.getGlyphID(0x1F44B);  // 👋
  auto glyphW = font.getGlyphID('W');
  auto glyphO = font.getGlyphID('o');
  auto glyphR = font.getGlyphID('r');
  auto glyphL = font.getGlyphID('l');
  auto glyphD = font.getGlyphID('d');

  TextBlobBuilder builder;
  float x = 47.0f;
  float y = 83.0f;

  // Run 1: "Hi" with horizontal positioning
  const auto& buffer1 = builder.allocRunPosH(font, 2, y);
  buffer1.glyphs[0] = glyphH;
  buffer1.glyphs[1] = glyphI;
  buffer1.positions[0] = x;
  buffer1.positions[1] = x + font.getAdvance(glyphH);
  x += font.getAdvance(glyphH) + font.getAdvance(glyphI) + 5.0f;

  // Run 2: Emoji with point positioning
  const auto& buffer2 = builder.allocRunPos(emojiFont, 1);
  buffer2.glyphs[0] = glyphEmoji;
  buffer2.positions[0] = x;
  buffer2.positions[1] = y;
  x += emojiFont.getAdvance(glyphEmoji) + 5.0f;

  // Run 3: "World" with horizontal positioning
  const auto& buffer3 = builder.allocRunPosH(font, 5, y);
  buffer3.glyphs[0] = glyphW;
  buffer3.glyphs[1] = glyphO;
  buffer3.glyphs[2] = glyphR;
  buffer3.glyphs[3] = glyphL;
  buffer3.glyphs[4] = glyphD;
  buffer3.positions[0] = x;
  x += font.getAdvance(glyphW);
  buffer3.positions[1] = x;
  x += font.getAdvance(glyphO);
  buffer3.positions[2] = x;
  x += font.getAdvance(glyphR);
  buffer3.positions[3] = x;
  x += font.getAdvance(glyphL);
  buffer3.positions[4] = x;

  auto blob = builder.build();
  ASSERT_TRUE(blob != nullptr);

  Paint paint;
  paint.setColor(Color::Black());
  canvas->drawTextBlob(blob, 0, 0, paint);

  context->flushAndSubmit();
  EXPECT_TRUE(Baseline::Compare(surface, "TextRenderTest/TextBlobMixedPositioning"));
}

TGFX_TEST(TextRenderTest, TextBlobWithStroke) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 248, 221);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSansSC-Regular.otf"));
  ASSERT_TRUE(typeface != nullptr);
  Font font(typeface, 60.0f);

  auto blob = TextBlob::MakeFrom("TGFX", font);
  ASSERT_TRUE(blob != nullptr);

  // Draw stroke
  Paint strokePaint;
  strokePaint.setColor(Color::Blue());
  strokePaint.setStyle(PaintStyle::Stroke);
  strokePaint.setStrokeWidth(3.0f);
  canvas->drawTextBlob(blob, 51.0f, 97.0f, strokePaint);

  // Draw fill on top
  Paint fillPaint;
  fillPaint.setColor(Color::FromRGBA(255, 200, 100, 255));
  canvas->drawTextBlob(blob, 51.0f, 97.0f, fillPaint);

  // Draw another with thicker stroke
  canvas->drawTextBlob(blob, 51.0f, 167.0f, strokePaint);
  strokePaint.setStrokeWidth(5.0f);
  strokePaint.setColor(Color::Red());
  canvas->drawTextBlob(blob, 51.0f, 167.0f, strokePaint);

  context->flushAndSubmit();
  EXPECT_TRUE(Baseline::Compare(surface, "TextRenderTest/TextBlobWithStroke"));
}

TGFX_TEST(TextRenderTest, TextBlobWithTransform) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 418, 356);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSansSC-Regular.otf"));
  ASSERT_TRUE(typeface != nullptr);
  Font font(typeface, 30.0f);

  auto blob = TextBlob::MakeFrom("Transform", font);
  ASSERT_TRUE(blob != nullptr);

  Paint paint;
  paint.setColor(Color::Black());

  // Normal
  canvas->drawTextBlob(blob, 56.0f, 74.0f, paint);

  // Scaled
  canvas->save();
  canvas->translate(56.0f, 134.0f);
  canvas->scale(1.5f, 1.5f);
  paint.setColor(Color::Blue());
  canvas->drawTextBlob(blob, 0, 0, paint);
  canvas->restore();

  // Rotated
  canvas->save();
  canvas->translate(236.0f, 234.0f);
  canvas->rotate(30);
  paint.setColor(Color::Red());
  canvas->drawTextBlob(blob, 0, 0, paint);
  canvas->restore();

  // Skewed
  canvas->save();
  canvas->translate(56.0f, 284.0f);
  canvas->skew(0.3f, 0);
  paint.setColor(Color::Green());
  canvas->drawTextBlob(blob, 0, 0, paint);
  canvas->restore();

  context->flushAndSubmit();
  EXPECT_TRUE(Baseline::Compare(surface, "TextRenderTest/TextBlobWithTransform"));
}

TGFX_TEST(TextRenderTest, ShapeMakeFromMixedTextBlob) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 100);
  auto canvas = surface->getCanvas();

  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSansSC-Regular.otf"));
  ASSERT_TRUE(typeface != nullptr);
  auto emojiTypeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoColorEmoji.ttf"));
  ASSERT_TRUE(emojiTypeface != nullptr);

  Font font(typeface, 60.0f);
  Font emojiFont(emojiTypeface, 60.0f);
  ASSERT_TRUE(font.hasOutlines());
  ASSERT_FALSE(emojiFont.hasOutlines());

  // Create a TextBlob with outline characters surrounding an emoji: "AB😀CD"
  auto glyphID_A = font.getGlyphID('A');
  auto glyphID_B = font.getGlyphID('B');
  auto glyphID_C = font.getGlyphID('C');
  auto glyphID_D = font.getGlyphID('D');
  auto glyphID_emoji = emojiFont.getGlyphID(0x1F600);
  ASSERT_TRUE(glyphID_A > 0);
  ASSERT_TRUE(glyphID_B > 0);
  ASSERT_TRUE(glyphID_C > 0);
  ASSERT_TRUE(glyphID_D > 0);
  ASSERT_TRUE(glyphID_emoji > 0);

  float advance_A = font.getAdvance(glyphID_A);
  float advance_B = font.getAdvance(glyphID_B);
  float advance_emoji = emojiFont.getAdvance(glyphID_emoji);
  float advance_C = font.getAdvance(glyphID_C);

  float x = 10.0f;
  float y = 70.0f;

  TextBlobBuilder builder;
  // Run 1: A
  const auto& buffer1 = builder.allocRunPos(font, 1);
  buffer1.glyphs[0] = glyphID_A;
  buffer1.positions[0] = x;
  buffer1.positions[1] = 0.0f;
  // Run 2: B
  const auto& buffer2 = builder.allocRunPos(font, 1);
  buffer2.glyphs[0] = glyphID_B;
  buffer2.positions[0] = x + advance_A;
  buffer2.positions[1] = 0.0f;
  // Run 3: emoji
  const auto& buffer3 = builder.allocRunPos(emojiFont, 1);
  buffer3.glyphs[0] = glyphID_emoji;
  buffer3.positions[0] = x + advance_A + advance_B;
  buffer3.positions[1] = 0.0f;
  // Run 4: C
  const auto& buffer4 = builder.allocRunPos(font, 1);
  buffer4.glyphs[0] = glyphID_C;
  buffer4.positions[0] = x + advance_A + advance_B + advance_emoji;
  buffer4.positions[1] = 0.0f;
  // Run 5: D
  const auto& buffer5 = builder.allocRunPos(font, 1);
  buffer5.glyphs[0] = glyphID_D;
  buffer5.positions[0] = x + advance_A + advance_B + advance_emoji + advance_C;
  buffer5.positions[1] = 0.0f;
  auto textBlob = builder.build();
  ASSERT_TRUE(textBlob != nullptr);

  // Shape::MakeFrom should succeed with mixed TextBlob, extracting only outline runs
  auto shape = Shape::MakeFrom(textBlob);
  ASSERT_TRUE(shape != nullptr);

  // The shape should contain all outline glyphs (A, B, C, D) but not the emoji
  auto path = shape->getPath();
  EXPECT_FALSE(path.isEmpty());

  // Draw the shape path
  canvas->clear(Color::White());
  Paint paint = {};
  paint.setColor(Color::Black());
  path.transform(Matrix::MakeTrans(0, y));
  canvas->drawPath(path, paint);

  EXPECT_TRUE(Baseline::Compare(surface, "TextRenderTest/ShapeMakeFromMixedTextBlob"));
}

/**
 * Test TextBlob bounds and hit testing with RSXform (rotation/scale) positioning.
 * This test verifies that getBounds, getTightBounds, and hitTestPoint work correctly
 * for glyphs with complex transformations (not just simple position offsets).
 */
TGFX_TEST(TextRenderTest, TextBlobRSXformBounds) {
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSansSC-Regular.otf"));
  ASSERT_TRUE(typeface != nullptr);
  Font font(typeface, 40.0f);

  auto glyphA = font.getGlyphID('A');
  auto glyphB = font.getGlyphID('B');
  auto glyphC = font.getGlyphID('C');
  ASSERT_TRUE(glyphA > 0 && glyphB > 0 && glyphC > 0);

  // Create RSXform with various transformations:
  // Glyph A: identity at (50, 50)
  // Glyph B: 45 degree rotation at (150, 50)
  // Glyph C: 2x scale at (250, 50)
  float angle45 = 3.14159265f / 4.0f;
  float cos45 = std::cos(angle45);
  float sin45 = std::sin(angle45);

  GlyphID glyphs[] = {glyphA, glyphB, glyphC};
  RSXform xforms[] = {
      RSXform::Make(1.0f, 0.0f, 50.0f, 50.0f),     // Identity
      RSXform::Make(cos45, sin45, 150.0f, 50.0f),  // 45 degree rotation
      RSXform::Make(2.0f, 0.0f, 250.0f, 50.0f),    // 2x scale
  };

  auto blob = TextBlob::MakeFromRSXform(glyphs, xforms, 3, font);
  ASSERT_TRUE(blob != nullptr);

  // Test getBounds - should return a valid bounding box
  auto bounds = blob->getBounds();
  EXPECT_FALSE(bounds.isEmpty());
  EXPECT_GT(bounds.right, bounds.left);

  // Test getTightBounds without matrix
  auto tightBounds = blob->getTightBounds();
  EXPECT_FALSE(tightBounds.isEmpty());
  // Tight bounds should be within or close to conservative bounds
  EXPECT_LE(tightBounds.left, bounds.right);
  EXPECT_GE(tightBounds.right, bounds.left);

  // Test getTightBounds with scale matrix (simulates layer scaling)
  Matrix scaleMatrix = Matrix::MakeScale(5.0f, 5.0f);
  auto scaledTightBounds = blob->getTightBounds(&scaleMatrix);
  EXPECT_FALSE(scaledTightBounds.isEmpty());
  // Scaled bounds should be approximately 5x the original
  auto expectedWidth = tightBounds.width() * 5.0f;
  auto expectedHeight = tightBounds.height() * 5.0f;
  EXPECT_NEAR(scaledTightBounds.width(), expectedWidth, expectedWidth * 0.1f);
  EXPECT_NEAR(scaledTightBounds.height(), expectedHeight, expectedHeight * 0.1f);

  // Test hitTestPoint using tightBounds center (guaranteed to be inside)
  float centerX = tightBounds.centerX();
  float centerY = tightBounds.centerY();
  // At least one of the glyphs should be hit at the center of the blob
  // This is a sanity check that hitTestPoint works at all
  bool hitSomewhere = blob->hitTestPoint(centerX, centerY) ||
                      blob->hitTestPoint(tightBounds.left + 10, centerY) ||
                      blob->hitTestPoint(tightBounds.right - 10, centerY);
  // Note: center may fall between glyphs, so we test multiple points
  EXPECT_TRUE(hitSomewhere || tightBounds.width() < 20);

  // Test hitTestPoint - miss (far outside all glyphs)
  EXPECT_FALSE(blob->hitTestPoint(-100.0f, -100.0f));
  EXPECT_FALSE(blob->hitTestPoint(500.0f, 500.0f));
}

/**
 * Test TextBlob with mixed positioning modes to ensure bounds and hit testing
 * work correctly for combined Point and RSXform runs.
 */
TGFX_TEST(TextRenderTest, TextBlobMixedPositioningBounds) {
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSansSC-Regular.otf"));
  ASSERT_TRUE(typeface != nullptr);
  Font font(typeface, 40.0f);

  auto glyphA = font.getGlyphID('A');
  auto glyphB = font.getGlyphID('B');
  auto glyphC = font.getGlyphID('C');
  auto glyphD = font.getGlyphID('D');
  ASSERT_TRUE(glyphA > 0 && glyphB > 0 && glyphC > 0 && glyphD > 0);

  TextBlobBuilder builder;

  // Run 1: Point positioning (simple offset)
  const auto& buffer1 = builder.allocRunPos(font, 2);
  buffer1.glyphs[0] = glyphA;
  buffer1.glyphs[1] = glyphB;
  reinterpret_cast<Point*>(buffer1.positions)[0] = Point::Make(0.0f, 50.0f);
  reinterpret_cast<Point*>(buffer1.positions)[1] = Point::Make(50.0f, 50.0f);

  // Run 2: RSXform positioning (rotation)
  float angle = 45.0f * 3.14159265f / 180.0f;
  float scos = std::cos(angle);
  float ssin = std::sin(angle);
  const auto& buffer2 = builder.allocRunRSXform(font, 2);
  buffer2.glyphs[0] = glyphC;
  buffer2.glyphs[1] = glyphD;
  reinterpret_cast<RSXform*>(buffer2.positions)[0] = RSXform::Make(scos, ssin, 150.0f, 50.0f);
  reinterpret_cast<RSXform*>(buffer2.positions)[1] = RSXform::Make(scos, ssin, 200.0f, 50.0f);

  auto blob = builder.build();
  ASSERT_TRUE(blob != nullptr);

  // getBounds should cover all glyphs
  auto bounds = blob->getBounds();
  EXPECT_FALSE(bounds.isEmpty());
  EXPECT_GT(bounds.width(), 100.0f);  // Should span across multiple glyphs

  // getTightBounds should also work
  auto tightBounds = blob->getTightBounds();
  EXPECT_FALSE(tightBounds.isEmpty());

  // getTightBounds with scale matrix
  Matrix scaleMatrix = Matrix::MakeScale(3.0f, 3.0f);
  auto scaledBounds = blob->getTightBounds(&scaleMatrix);
  EXPECT_FALSE(scaledBounds.isEmpty());
  // Width should be approximately 3x
  EXPECT_NEAR(scaledBounds.width(), tightBounds.width() * 3.0f, tightBounds.width() * 0.3f);

  // Verify hitTestPoint works - test points inside the tight bounds
  // At least one point in the bounds should hit a glyph
  bool hitAny = false;
  for (float x = tightBounds.left; x <= tightBounds.right && !hitAny; x += 10.0f) {
    for (float y = tightBounds.top; y <= tightBounds.bottom && !hitAny; y += 10.0f) {
      if (blob->hitTestPoint(x, y)) {
        hitAny = true;
      }
    }
  }
  EXPECT_TRUE(hitAny);

  // Verify hitTestPoint returns false for points far outside
  EXPECT_FALSE(blob->hitTestPoint(-200.0f, -200.0f));
  EXPECT_FALSE(blob->hitTestPoint(500.0f, 500.0f));
}

TGFX_TEST(TextRenderTest, AxisAlignedRotationRender) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 500, 500);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSansSC-Regular.otf"));
  ASSERT_TRUE(typeface != nullptr);
  Font font(typeface, 40.0f);

  auto glyphA = font.getGlyphID('A');
  auto glyphB = font.getGlyphID('B');
  auto glyphC = font.getGlyphID('C');
  auto glyphD = font.getGlyphID('D');
  ASSERT_TRUE(glyphA > 0 && glyphB > 0 && glyphC > 0 && glyphD > 0);

  Paint paint;
  paint.setColor(Color::Black());

  // Row 1: RSXform with exact axis-aligned rotations (0/90/180/270)
  {
    GlyphID glyphs[] = {glyphA, glyphB, glyphC, glyphD};
    float angles[] = {0.0f, 90.0f, 180.0f, 270.0f};
    TextBlobBuilder builder;
    const auto& buffer = builder.allocRunRSXform(font, 4);
    for (int i = 0; i < 4; i++) {
      buffer.glyphs[i] = glyphs[i];
      auto bounds = font.getBounds(glyphs[i]);
      float cx = bounds.centerX();
      float cy = bounds.centerY();
      float rad = angles[i] * static_cast<float>(M_PI) / 180.0f;
      float scos = cosf(rad);
      float ssin = sinf(rad);
      float tx = 75.0f + static_cast<float>(i) * 100.0f - (scos * cx - ssin * cy);
      float ty = 75.0f - (ssin * cx + scos * cy);
      reinterpret_cast<RSXform*>(buffer.positions)[i] = RSXform::Make(scos, ssin, tx, ty);
    }
    auto blob = builder.build();
    canvas->drawTextBlob(blob, 0, 0, paint);
  }

  // Row 2: RSXform with exact values (no trig functions) for axis-aligned rotations
  {
    GlyphID glyphs[] = {glyphA, glyphB, glyphC, glyphD};
    float scosValues[] = {1.0f, 0.0f, -1.0f, 0.0f};
    float ssinValues[] = {0.0f, 1.0f, 0.0f, -1.0f};
    TextBlobBuilder builder;
    const auto& buffer = builder.allocRunRSXform(font, 4);
    for (int i = 0; i < 4; i++) {
      buffer.glyphs[i] = glyphs[i];
      auto bounds = font.getBounds(glyphs[i]);
      float cx = bounds.centerX();
      float cy = bounds.centerY();
      float scos = scosValues[i];
      float ssin = ssinValues[i];
      float tx = 75.0f + static_cast<float>(i) * 100.0f - (scos * cx - ssin * cy);
      float ty = 200.0f - (ssin * cx + scos * cy);
      reinterpret_cast<RSXform*>(buffer.positions)[i] = RSXform::Make(scos, ssin, tx, ty);
    }
    auto blob = builder.build();
    canvas->drawTextBlob(blob, 0, 0, paint);
  }

  // Row 3: Matrix positioning with axis-aligned rotations (90/180) and scale (2x at 0 deg)
  {
    TextBlobBuilder builder;
    const auto& buffer = builder.allocRunMatrix(font, 3);
    buffer.glyphs[0] = glyphA;
    buffer.glyphs[1] = glyphB;
    buffer.glyphs[2] = glyphC;
    // Glyph A: 2x scale (axis-aligned, no rotation)
    buffer.positions[0] = 2.0f;
    buffer.positions[1] = 0.0f;
    buffer.positions[2] = 50.0f;
    buffer.positions[3] = 0.0f;
    buffer.positions[4] = 2.0f;
    buffer.positions[5] = 290.0f;
    // Glyph B: 90 degree rotation via matrix
    buffer.positions[6] = 0.0f;
    buffer.positions[7] = -1.0f;
    buffer.positions[8] = 225.0f;
    buffer.positions[9] = 1.0f;
    buffer.positions[10] = 0.0f;
    buffer.positions[11] = 290.0f;
    // Glyph C: 180 degree rotation via matrix
    buffer.positions[12] = -1.0f;
    buffer.positions[13] = 0.0f;
    buffer.positions[14] = 350.0f;
    buffer.positions[15] = 0.0f;
    buffer.positions[16] = -1.0f;
    buffer.positions[17] = 350.0f;
    auto blob = builder.build();
    canvas->drawTextBlob(blob, 0, 0, paint);
  }

  // Row 4: Non-axis-aligned rotation (45 degrees) for comparison, rendered via path
  {
    GlyphID glyphs[] = {glyphA, glyphB};
    TextBlobBuilder builder;
    const auto& buffer = builder.allocRunRSXform(font, 2);
    for (int i = 0; i < 2; i++) {
      buffer.glyphs[i] = glyphs[i];
      auto bounds = font.getBounds(glyphs[i]);
      float cx = bounds.centerX();
      float cy = bounds.centerY();
      float rad = 45.0f * static_cast<float>(M_PI) / 180.0f;
      float scos = cosf(rad);
      float ssin = sinf(rad);
      float tx = 125.0f + static_cast<float>(i) * 150.0f - (scos * cx - ssin * cy);
      float ty = 430.0f - (ssin * cx + scos * cy);
      reinterpret_cast<RSXform*>(buffer.positions)[i] = RSXform::Make(scos, ssin, tx, ty);
    }
    auto blob = builder.build();
    canvas->drawTextBlob(blob, 0, 0, paint);
  }

  context->flushAndSubmit();
  EXPECT_TRUE(Baseline::Compare(surface, "TextRenderTest/AxisAlignedRotationRender"));
}

TGFX_TEST(TextRenderTest, VerticalTextLayout) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSansSC-Regular.otf"));
  ASSERT_TRUE(typeface != nullptr);

  std::string text = "你好，测试。Hi,Tgfx.";
  float fontSize = 50.0f;
  Font font(typeface, fontSize);
  auto metrics = font.getMetrics();

  // Collect glyph IDs and unichars.
  std::vector<GlyphID> glyphIDs = {};
  std::vector<Unichar> unichars = {};
  const char* ptr = text.data();
  const char* end = text.data() + text.size();
  while (ptr < end) {
    auto unichar = UTF::NextUTF8(&ptr, end);
    if (unichar < 0) {
      break;
    }
    auto glyphID = font.getGlyphID(unichar);
    if (glyphID == 0) {
      continue;
    }
    glyphIDs.push_back(glyphID);
    unichars.push_back(unichar);
  }

  // Measure total column height. Latin characters use horizontal advance as the vertical step
  // after rotation, while CJK characters use the font's vertical advance.
  float columnHeight = 0.0f;
  for (size_t i = 0; i < glyphIDs.size(); i++) {
    bool isLatin = (unichars[i] >= 0x0020 && unichars[i] <= 0x007E);
    columnHeight += isLatin ? font.getAdvance(glyphIDs[i], false) : font.getAdvance(glyphIDs[i], true);
  }

  float margin = 50.0f;
  auto surfaceWidth = static_cast<int>(ceilf(fontSize + margin * 2.0f));
  auto surfaceHeight = static_cast<int>(ceilf(columnHeight + margin * 2.0f));
  auto surface = Surface::Make(context, surfaceWidth, surfaceHeight);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  Paint paint;
  paint.setColor(Color::Black());

  float centerX = static_cast<float>(surfaceWidth) * 0.5f;
  float y = margin;
  for (size_t i = 0; i < glyphIDs.size(); i++) {
    auto glyphID = glyphIDs[i];
    auto unichar = unichars[i];
    // Latin characters (U+0020..U+007E) are rotated 90 degrees clockwise and use horizontal
    // advance as the vertical step. CJK and other characters use the font's built-in vertical
    // offset and vertical advance.
    bool isLatin = (unichar >= 0x0020 && unichar <= 0x007E);
    float step = 0.0f;
    if (isLatin) {
      auto horizontalAdvance = font.getAdvance(glyphID, false);
      step = horizontalAdvance;
      float cellCenterY = y + step * 0.5f;
      float glyphX = -horizontalAdvance * 0.5f;
      float glyphY = -(metrics.ascent + metrics.descent) * 0.5f;
      canvas->save();
      canvas->translate(centerX, cellCenterY);
      canvas->rotate(90.0f);
      auto glyphPos = Point::Make(glyphX, glyphY);
      canvas->drawGlyphs(&glyphID, &glyphPos, 1, font, paint);
      canvas->restore();
    } else {
      step = font.getAdvance(glyphID, true);
      auto offset = font.getVerticalOffset(glyphID);
      auto glyphPos = Point::Make(centerX + offset.x, y + offset.y);
      canvas->drawGlyphs(&glyphID, &glyphPos, 1, font, paint);
    }
    y += step;
  }

  context->flushAndSubmit();
  EXPECT_TRUE(Baseline::Compare(surface, "TextRenderTest/VerticalTextLayout"));
}

}  // namespace tgfx
