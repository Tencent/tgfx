/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "CustomLayer.h"
#include "base/LayerTreeDrawers.h"
#include "drawers/AppHost.h"
#include "tgfx/core/GlyphRun.h"
#include "tgfx/core/UTF.h"

namespace drawers {

std::shared_ptr<CustomLayer> CustomLayer::Make() {
  auto layer = std::shared_ptr<CustomLayer>(new CustomLayer());
  layer->weakThis = layer;
  return layer;
}

std::unique_ptr<tgfx::LayerContent> CustomLayer::onUpdateContent() {
  if (_text.empty()) {
    return nullptr;
  }
  std::vector<tgfx::GlyphID> glyphs = {};
  std::vector<tgfx::Point> positions = {};

  // use middle alignment, refer to the document: https://paddywang.github.io/demo/list/css/baseline_line-height.html
  auto metrics = _font.getMetrics();
  auto lineHeight = ceil(_font.getSize() * 1.2f);
  auto baseLine = (lineHeight + metrics.xHeight) / 2;
  auto emptyGlyphID = _font.getGlyphID(" ");
  auto emptyAdvance = _font.getAdvance(emptyGlyphID);
  const char* textStart = _text.data();
  const char* textStop = textStart + _text.size();
  float xOffset = 0;
  float yOffset = baseLine;
  while (textStart < textStop) {
    if (*textStart == '\n') {
      xOffset = 0;
      yOffset += lineHeight;
      textStart++;
      continue;
    }
    auto unichar = tgfx::UTF::NextUTF8(&textStart, textStop);
    auto glyphID = _font.getGlyphID(unichar);
    if (glyphID > 0) {
      glyphs.push_back(glyphID);
      positions.push_back(tgfx::Point::Make(xOffset, yOffset));
      auto advance = _font.getAdvance(glyphID);
      xOffset += advance;
    } else {
      glyphs.push_back(emptyGlyphID);
      positions.push_back(tgfx::Point::Make(xOffset, yOffset));
      xOffset += emptyAdvance;
    }
  }
  tgfx::GlyphRun glyphRun(_font, std::move(glyphs), std::move(positions));
  auto textBlob = tgfx::TextBlob::MakeFrom(std::move(glyphRun));
  if (textBlob == nullptr) {
    return nullptr;
  }
  return std::make_unique<CustomLayerContent>(std::move(textBlob));
}

std::shared_ptr<tgfx::Layer> CustomLayerTree::buildLayerTree(const AppHost* host) {
  auto customLayer = CustomLayer::Make();
  customLayer->setText("Hello TGFX.\nThis is a custom layer");
  customLayer->setFont(tgfx::Font(host->getTypeface("default"), 40));
  return customLayer;
}

};  // namespace drawers
