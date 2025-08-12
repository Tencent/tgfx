/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "SimpleTextLayer.h"
#include <tgfx/layers/Gradient.h>
#include <tgfx/layers/ShapeLayer.h>
#include <tgfx/layers/SolidColor.h>
#include "base/Drawers.h"
#include "drawers/AppHost.h"
#include "tgfx/core/UTF.h"

namespace drawers {
static auto strokeOffset = 0.f;

// Merge continuous lines at the same height
static void mergeLines(std::vector<TextLine>& lines) {
  if (lines.empty()) return;

  std::sort(lines.begin(), lines.end(), [](const TextLine& a, const TextLine& b) {
    if (a.linePosition != b.linePosition) {
      return a.linePosition < b.linePosition;
    }
    return a.left < b.left;
  });

  std::vector<TextLine> merged;
  merged.push_back(lines[0]);

  for (size_t i = 1; i < lines.size(); ++i) {
    TextLine& last = merged.back();
    TextLine& current = lines[i];

    if (current.linePosition == last.linePosition && current.left <= last.right) {
      last.right = std::max(last.right, current.right);
    } else {
      merged.push_back(current);
    }
  }

  lines = std::move(merged);
}

// Set the positions of the first occurrence of B in string A to true, for quickly setting underlineIndex and deletelineIndex
std::vector<bool> findFirstOccurrence(const std::string& A, const std::string& B) {
  std::vector<bool> result(A.size(), false);
  if (B.empty() || A.empty() || B.size() > A.size()) {
    return result;
  }

  size_t foundPos = A.find(B);
  if (foundPos != std::string::npos) {
    for (size_t i = foundPos; i < foundPos + B.size(); ++i) {
      result[i] = true;
    }
  }
  return result;
}

std::shared_ptr<SimpleTextLayer> SimpleTextLayer::Make() {
  return std::shared_ptr<SimpleTextLayer>(new SimpleTextLayer());
}

void SimpleTextLayer::onUpdateContent(tgfx::LayerRecorder* recorder) {
  auto canvas = recorder->getCanvas();

  auto linePaint = tgfx::Paint();
  linePaint.setColor(tgfx::Color::Black());
  linePaint.setStyle(tgfx::PaintStyle::Stroke);
  linePaint.setStrokeWidth(1.0f);
  for (auto& richText : richTexts) {
    for (const auto& [left, right, linePosition] : richText.underline) {
      canvas->drawLine(left, linePosition, right, linePosition, linePaint);
    }

    if (richText.type == Element::Text) {
      for (const auto& paint : richText.paints) {
        canvas->drawTextBlob(richText.textBlob, 0, 0, paint);
      }
    } else {
      canvas->drawImageRect(richText.image, richText.imageRect);
    }

    for (const auto& [left, right, linePosition] : richText.deleteline) {
      canvas->drawLine(left, linePosition, right, linePosition, linePaint);
    }
  }
}

void SimpleTextLayer::invalidateLayout() {
  auto lineTop = 0.f;          // Total height of all previous lines
  auto lineHeight = 0.f;       // Current line's max height
  auto baselineHeight = 0.f;   // Current line's max baseline height
  auto underlineHeight = 0.f;  // Current line's max underline height

  std::vector<float> baselines;   // Baseline positions for each line
  std::vector<float> underlines;  // Underline positions for each line

  // Baselines and underlines height are determined by the tallest richText, so calculates theses in first pass
  for (auto& richText : richTexts) {
    if (richText.type == Element::Text) {
      auto font = richText.font;
      auto metrics = font.getMetrics();
      float textHeight = ceil(font.getSize() * 1.2f);
      float textBaseline = (textHeight + metrics.xHeight) / 2.f;
      float textUnderline = textBaseline + metrics.descent;
      lineHeight = std::max(lineHeight, textHeight);
      baselineHeight = std::max(baselineHeight, textBaseline);
      underlineHeight = std::max(underlineHeight, textUnderline + strokeOffset);
      for (auto& c : richText.text) {
        if (c == '\n') {
          baselines.push_back(lineTop + baselineHeight);
          underlines.push_back(lineTop + underlineHeight);
          lineTop += lineHeight;
          lineHeight = textHeight;
          baselineHeight = textBaseline;
          underlineHeight = textUnderline;
        }
      }
    } else if (richText.type == Element::Image) {
      lineHeight = lineHeight == 0.f
                       ? richText.height * 1.2f
                       : std::max(lineHeight, lineHeight - baselineHeight + richText.height);
      baselineHeight = std::max(baselineHeight, richText.height);
    }
  }
  baselines.push_back(lineTop + baselineHeight);
  underlines.push_back(lineTop + underlineHeight);

  // Calculate position for each richText and record text lines
  auto xOffset = 0.f;
  size_t lineIndex = 0;
  for (auto& richText : richTexts) {
    if (richText.type == Element::Text) {
      std::vector<tgfx::GlyphID> glyphs = {};
      std::vector<tgfx::Point> positions = {};
      size_t index = 0;
      const char* textStart = richText.text.data();
      const char* textStop = textStart + richText.text.size();
      auto font = richText.font;
      auto metrics = font.getMetrics();
      auto emptyGlyphID = font.getGlyphID(" ");
      auto emptyAdvance = font.getAdvance(emptyGlyphID);
      while (textStart < textStop) {
        if (*textStart == '\n') {
          xOffset = 0;
          lineIndex++;
          textStart++;
          index++;
          continue;
        }
        auto left = xOffset;
        auto unichar = tgfx::UTF::NextUTF8(&textStart, textStop);
        auto glyphID = font.getGlyphID(unichar);
        if (glyphID > 0) {
          glyphs.push_back(glyphID);
          positions.push_back(tgfx::Point::Make(xOffset, baselines[lineIndex]));
          auto advance = font.getAdvance(glyphID);
          xOffset += advance;
        } else {
          glyphs.push_back(emptyGlyphID);
          positions.push_back(tgfx::Point::Make(xOffset, baselines[lineIndex]));
          xOffset += emptyAdvance;
        }

        if (!richText.underlineIndex.empty() && richText.underlineIndex[index]) {
          richText.underline.push_back({left, xOffset, underlines[lineIndex]});
        }
        if (!richText.deletelineIndex.empty() && richText.deletelineIndex[index]) {
          richText.deleteline.push_back(
              {left, xOffset, baselines[lineIndex] - metrics.xHeight / 2});
        }
        index++;
      }
      tgfx::GlyphRun glyphRun(font, std::move(glyphs), std::move(positions));
      richText.textBlob = tgfx::TextBlob::MakeFrom(std::move(glyphRun));
    } else if (richText.type == Element::Image) {
      richText.imageRect = tgfx::Rect::MakeXYWH(xOffset, baselines[lineIndex] - richText.height,
                                                richText.width, richText.height);
      if (!richText.underlineIndex.empty()) {
        auto left = xOffset;
        richText.underline.push_back({left, left + richText.width, underlines[lineIndex]});
      }
      if (!richText.deletelineIndex.empty()) {
        auto left = xOffset;
        richText.deleteline.push_back(
            {left, left + richText.width, baselines[lineIndex] - richText.height / 2});
      }
      xOffset += richText.width;
    }
  }

  for (auto& richText : richTexts) {
    mergeLines(richText.underline);
    mergeLines(richText.deleteline);
  }
}

std::shared_ptr<tgfx::Layer> MakeDebugLayer(std::shared_ptr<tgfx::Layer>& root,
                                            const tgfx::Color& color = {1, 0, 0, 0.5}) {
  auto bounds = root->getBounds(nullptr, true);
  auto layer = tgfx::ShapeLayer::Make();
  tgfx::Path path;
  path.addRect(bounds);
  layer->setFillStyle(tgfx::SolidColor::Make(color));
  layer->setPath(path);
  return layer;
}

std::shared_ptr<tgfx::Layer> SimpleText::buildLayerTree(const AppHost* host) {
  auto root = tgfx::Layer::Make();

  padding = 50.f;
  auto screenWidth = 600.f;

  std::vector<std::string> texts = {"HelloTGFX!", "\nTGFX",
                                    "(Tencent Graphics) is a lightweight 2D graphics \nlibrary for "
                                    "rendering text, shapes,video and images.\n",
                                    "🤡👻🐠🤩😃🤪🙈🙊🐒🐙‍"};

  std::vector<tgfx::Font> fonts;
  auto typeface = host->getTypeface("default");
  tgfx::Font font(typeface, 60);
  font.setFauxBold(true);
  fonts.push_back(font);
  font = tgfx::Font(typeface, 21);
  font.setFauxBold(true);
  fonts.push_back(font);
  font = tgfx::Font(typeface, 15);
  font.setFauxBold(false);
  font.setFauxItalic(true);
  fonts.push_back(font);
  typeface = host->getTypeface("emoji");
  font = tgfx::Font(typeface, 30);
  fonts.push_back(font);

  std::vector<tgfx::Paint> paints;
  tgfx::Paint paint = {};
  paint.setColor({1.0f, 1.0f, 1.0f, 1.0f});
  paint.setStyle(tgfx::PaintStyle::Stroke);
  paint.setStrokeWidth(2);
  //strokeOffset = 2;
  paints.push_back(paint);
  paint = {};
  paint.setStyle(tgfx::PaintStyle::Fill);
  tgfx::Color cyan = {0.0f, 1.0f, 1.0f, 1.0f};
  tgfx::Color magenta = {1.0f, 0.0f, 1.0f, 1.0f};
  tgfx::Color yellow = {1.0f, 1.0f, 0.0f, 1.0f};
  auto startPoint = tgfx::Point::Make(0.0f, 0.0f);
  auto endPoint = tgfx::Point::Make(600.f, 0.0f);
  auto shader = tgfx::Shader::MakeLinearGradient(startPoint, endPoint, {cyan, magenta, yellow}, {});
  paint.setShader(shader);
  paints.push_back(paint);
  paints.push_back({});

  std::vector<Element> elements;
  //Image
  Element element;
  element.type = Element::Image;
  auto image = host->getImage("TGFX");
  image = image->makeMipmapped(true);
  element.image = image;
  auto textHeight = ceil(fonts[0].getSize() * 0.8f);
  element.height = textHeight;
  element.width =
      static_cast<float>(image->width()) * element.height / static_cast<float>(image->height());
  elements.push_back(element);
  //HelloTGFX!
  element = Element{};
  element.text = texts[0];
  element.font = fonts[0];
  element.paints = {paints[0], paints[1]};
  elements.push_back(element);
  //TGFX
  element = Element{};
  element.text = texts[1];
  element.font = fonts[1];
  element.paints = {paints[0], paints[1]};
  element.underlineIndex = std::vector(texts[1].size(), true);
  elements.push_back(element);
  //.....
  element = Element{};
  element.text = texts[2];
  element.font = fonts[2];
  element.paints = {paints[0], paints[1]};
  element.underlineIndex = findFirstOccurrence(texts[2], "(Tencent Graphics)");
  element.deletelineIndex = findFirstOccurrence(texts[2], "video");
  elements.push_back(element);
  //emoji
  element = Element{};
  element.text = texts[3];
  element.font = fonts[3];
  element.paints = {paints[2]};
  elements.push_back(element);

  auto textLayer = SimpleTextLayer::Make();
  textLayer->setElements(elements);
  textLayer->invalidateLayout();
  auto bounds = textLayer->getBounds();
  auto textScale = screenWidth / bounds.width();
  tgfx::Matrix matrix = tgfx::Matrix::MakeScale(textScale, textScale);
  textLayer->setMatrix(matrix);

  root->addChild(textLayer);
  return root;
}

};  // namespace drawers
