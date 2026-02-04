/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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
#include "tgfx/core/Font.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Surface.h"
#include "tgfx/core/TextBlob.h"
#include "utils/TestUtils.h"

namespace tgfx {

// This test verifies getVerticalOffset by comparing two rendering approaches:
// 1. LEFT: Draw glyph at H origin, then show where V would be (V = H - offset)
// 2. RIGHT: Draw glyph at V origin using vertical text positioning
// The offset satisfies: H = V + offset
// If the offset is correct, both glyphs should appear identical.
TGFX_TEST(VerticalOffsetTest, OffsetVerification) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr);

  float fontSize = 80.0f;
  Font font(typeface, fontSize);

  GlyphID glyphID = font.getGlyphID("g");
  ASSERT_NE(glyphID, 0u);

  Point offset = font.getVerticalOffset(glyphID);
  Rect bounds = font.getBounds(glyphID);

  int canvasWidth = 500;
  int canvasHeight = 300;

  auto surface = Surface::Make(context, canvasWidth, canvasHeight);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  Paint axisPaint = {};
  axisPaint.setStyle(PaintStyle::Stroke);
  axisPaint.setStrokeWidth(2.0f);

  Paint textPaint = {};
  textPaint.setColor(Color::Black());

  Paint pointPaint = {};
  pointPaint.setStyle(PaintStyle::Fill);

  Font labelFont(typeface, 14.0f);
  Paint labelPaint = {};
  labelPaint.setColor(Color::Black());

  // === LEFT SIDE: Horizontal origin system ===
  float leftCenterX = 125.0f;
  float hY = 180.0f;  // H origin Y position (baseline)
  float hX = leftCenterX - bounds.centerX();  // Center the glyph horizontally

  // Draw H origin axes (green)
  axisPaint.setColor(Color::FromRGBA(0, 180, 0, 200));
  canvas->drawLine(hX - 50, hY, hX + 100, hY, axisPaint);  // Baseline
  canvas->drawLine(hX, hY - 80, hX, hY + 50, axisPaint);   // Vertical

  // Draw glyph at H origin
  std::vector<GlyphID> glyphs = {glyphID};
  std::vector<Point> posH = {{hX, hY}};
  auto blobH = TextBlob::MakeFrom(&glyphs[0], &posH[0], 1, font);
  if (blobH) {
    canvas->drawTextBlob(blobH, 0, 0, textPaint);
  }

  // Calculate V position: V = H - offset (since H = V + offset)
  float vX = hX - offset.x;
  float vY = hY - offset.y;

  // Draw V origin point (blue circle)
  pointPaint.setColor(Color::FromRGBA(0, 0, 255, 255));
  canvas->drawCircle(vX, vY, 5.0f, pointPaint);

  // Draw H origin point (green circle)
  pointPaint.setColor(Color::FromRGBA(0, 180, 0, 255));
  canvas->drawCircle(hX, hY, 5.0f, pointPaint);

  // Labels for left side
  auto labelH = TextBlob::MakeFrom("H (green)", labelFont);
  auto labelV = TextBlob::MakeFrom("V (blue)", labelFont);
  if (labelH) {
    canvas->drawTextBlob(labelH, hX + 10, hY + 40, labelPaint);
  }
  if (labelV) {
    canvas->drawTextBlob(labelV, vX + 10, vY - 10, labelPaint);
  }

  // === RIGHT SIDE: Vertical origin system ===
  float rightCenterX = 375.0f;
  // V origin position - we want the glyph to appear at the same visual position
  float vX2 = rightCenterX - bounds.centerX() - offset.x;
  float vY2 = vY;  // Same Y as the V point calculated from left side

  // Draw V origin axes (blue)
  axisPaint.setColor(Color::FromRGBA(0, 0, 255, 200));
  canvas->drawLine(vX2 - 50, vY2, vX2 + 100, vY2, axisPaint);  // Horizontal
  canvas->drawLine(vX2, vY2 - 80, vX2, vY2 + 80, axisPaint);   // Vertical

  // To draw glyph at V origin, we need H = V + offset
  float hX2 = vX2 + offset.x;
  float hY2 = vY2 + offset.y;

  // Draw glyph (using H position since TextBlob uses horizontal origin)
  std::vector<Point> posV = {{hX2, hY2}};
  auto blobV = TextBlob::MakeFrom(&glyphs[0], &posV[0], 1, font);
  if (blobV) {
    canvas->drawTextBlob(blobV, 0, 0, textPaint);
  }

  // Draw V origin point (blue circle)
  pointPaint.setColor(Color::FromRGBA(0, 0, 255, 255));
  canvas->drawCircle(vX2, vY2, 5.0f, pointPaint);

  // Title
  auto title = TextBlob::MakeFrom("Offset Verification: H origin vs V origin", labelFont);
  if (title) {
    canvas->drawTextBlob(title, 100, 25, labelPaint);
  }

  // Section labels
  auto leftLabel = TextBlob::MakeFrom("H origin (baseline)", labelFont);
  auto rightLabel = TextBlob::MakeFrom("V origin (vertical)", labelFont);
  if (leftLabel) {
    canvas->drawTextBlob(leftLabel, 60, 50, labelPaint);
  }
  if (rightLabel) {
    canvas->drawTextBlob(rightLabel, 310, 50, labelPaint);
  }

  // Offset info
  char offsetStr[64] = {};
  snprintf(offsetStr, sizeof(offsetStr), "offset = (%.1f, %.1f)", offset.x, offset.y);
  auto offsetLabel = TextBlob::MakeFrom(offsetStr, labelFont);
  if (offsetLabel) {
    canvas->drawTextBlob(offsetLabel, 180, canvasHeight - 20, labelPaint);
  }

  EXPECT_TRUE(Baseline::Compare(surface, "VerticalOffsetTest/OffsetVerification"));
}

// Verify that multiple glyphs align horizontally when using V origin for vertical text.
TGFX_TEST(VerticalOffsetTest, VerticalAlignment) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr);

  float fontSize = 50.0f;
  Font font(typeface, fontSize);

  // Test various character types
  std::vector<std::string> chars = {
      "中",  // Chinese
      "A",   // English uppercase
      "g",   // English lowercase
      "5",   // Arabic numeral
      "，",  // Full-width comma
      ",",   // Half-width comma
      "。",  // Full-width period
      ".",   // Half-width period
  };

  std::vector<GlyphID> glyphIDs = {};
  for (const auto& c : chars) {
    GlyphID id = font.getGlyphID(c);
    ASSERT_NE(id, 0u) << "Failed to get glyph for: " << c;
    glyphIDs.push_back(id);
  }

  int canvasWidth = 250;
  int canvasHeight = 550;

  auto surface = Surface::Make(context, canvasWidth, canvasHeight);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  Paint axisPaint = {};
  axisPaint.setStyle(PaintStyle::Stroke);
  axisPaint.setStrokeWidth(1.0f);
  axisPaint.setColor(Color::FromRGBA(0, 0, 255, 150));

  Paint textPaint = {};
  textPaint.setColor(Color::Black());

  Paint centerLinePaint = {};
  centerLinePaint.setStyle(PaintStyle::Stroke);
  centerLinePaint.setStrokeWidth(1.0f);
  centerLinePaint.setColor(Color::FromRGBA(255, 0, 0, 120));

  Font labelFont(typeface, 12.0f);
  Paint labelPaint = {};
  labelPaint.setColor(Color::Black());

  // V axis position (vertical text alignment axis)
  float vAxisX = 125.0f;
  float startY = 60.0f;
  float spacing = 60.0f;

  // Draw vertical V axis
  canvas->drawLine(vAxisX, 40, vAxisX, canvasHeight - 50, axisPaint);

  // Draw each glyph centered on V axis
  for (size_t i = 0; i < glyphIDs.size(); i++) {
    GlyphID glyphID = glyphIDs[i];
    Point offset = font.getVerticalOffset(glyphID);
    Rect bounds = font.getBounds(glyphID);

    float vY = startY + static_cast<float>(i) * spacing;

    // H position = V position + offset (since H = V + offset)
    float hX = vAxisX + offset.x;
    float hY = vY + offset.y;

    // Draw glyph at H position
    std::vector<GlyphID> glyph = {glyphID};
    std::vector<Point> pos = {{hX, hY}};
    auto blob = TextBlob::MakeFrom(&glyph[0], &pos[0], 1, font);
    if (blob) {
      canvas->drawTextBlob(blob, 0, 0, textPaint);
    }

    // Draw glyph bounding box center line (should align with V axis)
    float glyphCenterX = hX + bounds.centerX();
    float glyphTop = hY + bounds.top;
    float glyphBottom = hY + bounds.bottom;

    canvas->drawLine(glyphCenterX, glyphTop, glyphCenterX, glyphBottom, centerLinePaint);
  }

  // Title
  auto title = TextBlob::MakeFrom("Vertical Alignment", labelFont);
  if (title) {
    canvas->drawTextBlob(title, 75, 25, labelPaint);
  }

  // Legend
  auto legend1 = TextBlob::MakeFrom("Blue: V axis", labelFont);
  auto legend2 = TextBlob::MakeFrom("Red: Glyph center", labelFont);
  if (legend1) {
    canvas->drawTextBlob(legend1, 70, canvasHeight - 35, labelPaint);
  }
  if (legend2) {
    canvas->drawTextBlob(legend2, 70, canvasHeight - 20, labelPaint);
  }

  EXPECT_TRUE(Baseline::Compare(surface, "VerticalOffsetTest/VerticalAlignment"));
}

}  // namespace tgfx
