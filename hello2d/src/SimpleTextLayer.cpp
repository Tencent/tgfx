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

#include "SimpleTextLayer.h"
#include <algorithm>
#include <map>
#include "base/LayerBuilders.h"
#include "hello2d/AppHost.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/UTF.h"
#include "tgfx/layers/LayerRecorder.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/vectors/Gradient.h"
#include "tgfx/layers/vectors/SolidColor.h"

namespace hello2d {

static tgfx::LayerPaint ToLayerPaint(const tgfx::Paint& paint) {
  tgfx::LayerPaint layerPaint;
  layerPaint.color = paint.getColor();
  layerPaint.shader = paint.getShader();
  layerPaint.blendMode = paint.getBlendMode();
  layerPaint.style = paint.getStyle();
  auto stroke = paint.getStroke();
  if (stroke != nullptr) {
    layerPaint.stroke = *stroke;
  }
  return layerPaint;
}

static bool CompareTextLine(const TextLine& a, const TextLine& b) {
  if (a.linePosition != b.linePosition) {
    return a.linePosition < b.linePosition;
  }
  return a.left < b.left;
}

static void MergeLines(std::vector<TextLine>& lines) {
  if (lines.empty()) {
    return;
  }

  std::sort(lines.begin(), lines.end(), CompareTextLine);

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
static std::vector<std::pair<size_t, size_t>> FindFirstOccurrence(const std::string& A,
                                                                  const std::string& B) {
  if (B.empty() || A.empty() || B.size() > A.size()) {
    return {};
  }

  std::vector<std::pair<size_t, size_t>> result = {};

  size_t foundPos = A.find(B);
  if (foundPos != std::string::npos) {
    result.emplace_back(foundPos, foundPos + B.size() - 1);
  }
  return result;
}

std::shared_ptr<SimpleTextLayer> SimpleTextLayer::Make() {
  return std::shared_ptr<SimpleTextLayer>(new SimpleTextLayer());
}

void SimpleTextLayer::onUpdateContent(tgfx::LayerRecorder* recorder) {
  updateLayout();
  auto linePaint = tgfx::Paint();
  linePaint.setColor(tgfx::Color::Black());
  linePaint.setStyle(tgfx::PaintStyle::Stroke);
  linePaint.setStrokeWidth(1.0f);

  auto lineLayerPaint = ToLayerPaint(linePaint);

  for (auto& richText : richTexts) {
    for (const auto& [left, right, linePosition] : richText.underline) {
      tgfx::Path linePath;
      linePath.moveTo(left, linePosition);
      linePath.lineTo(right, linePosition);
      recorder->addPath(linePath, lineLayerPaint);
    }

    if (richText.type == Element::Type::Text) {
      for (const auto& paint : richText.paints) {
        recorder->addTextBlob(richText.textBlob, ToLayerPaint(paint));
      }
    } else {
      if (richText.image) {
        tgfx::Path imagePath;
        imagePath.addRect(richText.imageRect);
        tgfx::LayerPaint imagePaint;
        float imgW = static_cast<float>(richText.image->width());
        float imgH = static_cast<float>(richText.image->height());
        if (imgW > 0.0f && imgH > 0.0f) {
          auto scaleX = richText.imageRect.width() / imgW;
          auto scaleY = richText.imageRect.height() / imgH;
          tgfx::Matrix m = tgfx::Matrix::MakeScale(scaleX, scaleY);
          m.postTranslate(richText.imageRect.left, richText.imageRect.top);
          imagePaint.shader = tgfx::Shader::MakeImageShader(richText.image)->makeWithMatrix(m);
        }
        recorder->addPath(imagePath, imagePaint);
      }
    }

    for (const auto& [left, right, linePosition] : richText.deleteline) {
      tgfx::Path linePath;
      linePath.moveTo(left, linePosition);
      linePath.lineTo(right, linePosition);
      recorder->addPath(linePath, lineLayerPaint);
    }
  }
}

void SimpleTextLayer::updateLayout() {
  auto lineTop = 0.f;
  auto lineHeight = 0.f;
  auto baselineHeight = 0.f;
  auto underlineHeight = 0.f;

  std::vector<float> baselines = {};
  std::vector<float> underlines = {};

  std::map<std::string, std::pair<tgfx::Font, tgfx::FontMetrics>> fontMetricsCache = {};

  for (auto& richText : richTexts) {
    if (richText.type == Element::Type::Text) {
      auto font = richText.font;
      std::string fontKey = std::to_string(reinterpret_cast<uintptr_t>(font.getTypeface().get())) +
                            "_" + std::to_string(font.getSize());

      tgfx::FontMetrics fontMetrics;
      auto it = fontMetricsCache.find(fontKey);
      if (it != fontMetricsCache.end()) {
        fontMetrics = it->second.second;
      } else {
        fontMetrics = font.getMetrics();
        fontMetricsCache[fontKey] = std::make_pair(font, fontMetrics);
      }
      float textHeight = std::fabs(fontMetrics.ascent) + std::fabs(fontMetrics.descent) +
                         std::fabs(fontMetrics.leading);
      float textBaseline = (textHeight + fontMetrics.xHeight) / 2.f;
      float textUnderline = textBaseline + fontMetrics.descent;
      lineHeight = std::max(lineHeight, textHeight);
      baselineHeight = std::max(baselineHeight, textBaseline);
      underlineHeight = std::max(underlineHeight, textUnderline);
      for (size_t i = 0; i < richText.text.size(); i++) {
        if (richText.text[i] == '\n') {
          baselines.push_back(lineTop + baselineHeight);
          underlines.push_back(lineTop + underlineHeight);
          lineTop += lineHeight;
          if (i == richText.text.size() - 1) {
            lineHeight = 0;
            baselineHeight = 0;
            underlineHeight = 0;
          } else {
            lineHeight = textHeight;
            baselineHeight = textBaseline;
            underlineHeight = textUnderline;
          }
        }
      }
    } else if (richText.type == Element::Type::Image) {
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
    if (richText.type == Element::Type::Text) {
      std::vector<tgfx::GlyphID> glyphs = {};
      std::vector<tgfx::Point> positions = {};
      size_t index = 0;
      const char* textStart = richText.text.data();
      const char* textStop = textStart + richText.text.size();
      auto font = richText.font;
      std::string fontKey = std::to_string(reinterpret_cast<uintptr_t>(font.getTypeface().get())) +
                            "_" + std::to_string(font.getSize());
      auto& metrics = fontMetricsCache[fontKey].second;
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

        if (!richText.underlineIndex.empty()) {
          for (const auto& [start, end] : richText.underlineIndex) {
            if (start <= index && end >= index) {
              richText.underline.push_back({left, xOffset, underlines[lineIndex]});
            }
          }
        }
        if (!richText.deletelineIndex.empty()) {
          for (const auto& [start, end] : richText.deletelineIndex) {
            if (start <= index && end >= index) {
              richText.deleteline.push_back(
                  {left, xOffset, baselines[lineIndex] - metrics.xHeight / 2});
            }
          }
        }
        index++;
      }
      richText.textBlob =
          tgfx::TextBlob::MakeFrom(glyphs.data(), positions.data(), glyphs.size(), font);
    } else if (richText.type == Element::Type::Image) {
      auto descent = underlines[lineIndex] - baselines[lineIndex];
      auto imageTop = baselines[lineIndex] - richText.height + descent * 0.5f;
      richText.imageRect = tgfx::Rect::MakeXYWH(xOffset, imageTop, richText.width, richText.height);
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
    MergeLines(richText.underline);
    MergeLines(richText.deleteline);
  }
}

std::shared_ptr<tgfx::Layer> RichText::onBuildLayerTree(const AppHost* host) {
  auto root = tgfx::Layer::Make();

  std::vector<std::string> texts = {"HelloTGFX!", "\nTGFX",
                                    "(Tencent Graphics) is a lightweight 2D graphics \nlibrary for "
                                    "rendering text, shapes,video and images.\n",
                                    "🤡👻🐠🤩😃🤪🙈🙊🐒🐙‍"};

  std::vector<tgfx::Font> fonts;
  fonts.reserve(4);
  auto typeface = host->getTypeface("default");
  tgfx::Font font(typeface, 60);
  font.setFauxBold(true);
  fonts.push_back(font);
  font = tgfx::Font(typeface, 21);
  font.setFauxBold(true);
  fonts.push_back(font);
  font = tgfx::Font(typeface, 15);
  fonts.push_back(font);
  font = tgfx::Font(typeface, 15);
  font.setFauxItalic(true);
  fonts.push_back(font);
  typeface = host->getTypeface("emoji");
  font = tgfx::Font(typeface, 30);
  fonts.push_back(font);

  std::vector<tgfx::Paint> paints(2);
  paints[0].setColor({1.0f, 1.0f, 1.0f, 1.0f});
  paints[0].setStyle(tgfx::PaintStyle::Stroke);
  paints[0].setStrokeWidth(2);
  paints[1].setStyle(tgfx::PaintStyle::Fill);
  tgfx::Color cyan = {0.0f, 1.0f, 1.0f, 1.0f};
  tgfx::Color magenta = {1.0f, 0.0f, 1.0f, 1.0f};
  tgfx::Color yellow = {1.0f, 1.0f, 0.0f, 1.0f};
  auto startPoint = tgfx::Point::Make(0.0f, 0.0f);
  // Use a large value for gradient width, it will be clipped to text bounds
  auto endPoint = tgfx::Point::Make(1000.f, 0.0f);
  auto shader = tgfx::Shader::MakeLinearGradient(startPoint, endPoint, {cyan, magenta, yellow}, {});
  paints[1].setShader(shader);

  std::vector<Element> elements(8);
  elements[0].type = Element::Type::Image;
  auto image = host->getImage("TGFX");
  image = image->makeMipmapped(true);
  elements[0].image = image;
  auto metrics = fonts[0].getMetrics();
  auto textHeight = ceil(metrics.capHeight + metrics.descent);
  elements[0].height = textHeight;
  elements[0].width =
      static_cast<float>(image->width()) * textHeight / static_cast<float>(image->height());
  elements[1].text = texts[0];
  elements[1].font = fonts[0];
  elements[1].paints = {paints[0], paints[1]};
  tgfx::Paint strokePaint = {};
  strokePaint.setColor(tgfx::Color::White());
  strokePaint.setStyle(tgfx::PaintStyle::Stroke);
  strokePaint.setStrokeWidth(3);
  tgfx::Paint fillPaint = {};
  fillPaint.setColor(tgfx::Color::Black());
  fillPaint.setStyle(tgfx::PaintStyle::Fill);

  elements[2].text = texts[1];
  elements[2].font = fonts[1];
  elements[2].paints = {strokePaint, fillPaint};
  elements[3].text = " (";
  elements[3].font = fonts[2];
  elements[3].paints = {strokePaint, fillPaint};
  elements[4].text = "Tencent Graphics";
  elements[4].font = fonts[3];
  elements[4].paints = {strokePaint, fillPaint};
  elements[4].underlineIndex = {{0, 15}};
  elements[5].text = ") ";
  elements[5].font = fonts[2];
  elements[5].paints = {strokePaint, fillPaint};
  elements[6].text =
      " is a lightweight 2D graphics \nlibrary for rendering text, shapes,video and images.\n";
  elements[6].font = fonts[2];
  elements[6].paints = {strokePaint, fillPaint};
  elements[6].deletelineIndex = FindFirstOccurrence(elements[6].text, "video");
  elements[7].text = texts[3];
  elements[7].font = fonts[4];
  elements[7].paints = {{}};

  auto textLayer = SimpleTextLayer::Make();
  textLayer->setElements(std::move(elements));

  root->addChild(textLayer);
  return root;
}

};  // namespace hello2d
