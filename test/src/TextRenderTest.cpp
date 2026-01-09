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

#include "gtest/gtest.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/core/Surface.h"
#include "tgfx/core/TextBlob.h"
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

  std::vector<GlyphRun> glyphRuns;
  glyphRuns.push_back(GlyphRun(font, {glyphID_O}, {Point::Make(0.0f, 0.0f)}));
  glyphRuns.push_back(GlyphRun(emojiFont, {glyphID_emoji}, {Point::Make(emojiOffsetX, 0.0f)}));
  auto textBlob = TextBlob::MakeFrom(std::move(glyphRuns));
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

  std::vector<GlyphRun> glyphRuns;
  glyphRuns.push_back(GlyphRun(font, {glyphID_A}, {Point::Make(x, 0.0f)}));
  glyphRuns.push_back(GlyphRun(font, {glyphID_B}, {Point::Make(x + advance_A, 0.0f)}));
  glyphRuns.push_back(
      GlyphRun(emojiFont, {glyphID_emoji}, {Point::Make(x + advance_A + advance_B, 0.0f)}));
  glyphRuns.push_back(
      GlyphRun(font, {glyphID_C}, {Point::Make(x + advance_A + advance_B + advance_emoji, 0.0f)}));
  glyphRuns.push_back(
      GlyphRun(font, {glyphID_D},
               {Point::Make(x + advance_A + advance_B + advance_emoji + advance_C, 0.0f)}));
  auto textBlob = TextBlob::MakeFrom(std::move(glyphRuns));
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

}  // namespace tgfx
