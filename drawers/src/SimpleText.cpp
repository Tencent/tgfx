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

#include "base/Drawers.h"

namespace tdraw {
void SimpleText::onDraw(tgfx::Canvas* canvas, const tdraw::AppHost* host) const {
  std::string text = "HelloTGFX";
  auto scale = host->density();
  auto width = static_cast<float>(host->width());
  auto height = static_cast<float>(host->height());
  auto typeface = host->getTypeface("default");
  tgfx::Font font(typeface, 40 * scale);
  font.setFauxBold(true);
  auto textBlob = tgfx::TextBlob::MakeFrom(text, font);
  auto bounds = textBlob->getBounds();
  auto matrix =
      tgfx::Matrix::MakeTrans((width - bounds.width()) / 2, height / 2 - bounds.centerY());
  auto screenWidth = width - (100.0f * scale);
  screenWidth = std::max(screenWidth, 300.0f);
  auto textScale = screenWidth / bounds.width();
  matrix.postScale(textScale, textScale, width / 2, height / 2);
  tgfx::Paint paint = {};
  std::vector<tgfx::Color> colors = {{0.0f, 0.85f, 0.35f, 1.0f}, {0.0f, 0.35f, 0.85f, 1.0f}};
  auto startPoint = tgfx::Point::Make(bounds.left, 0.0f);
  auto endPoint = tgfx::Point::Make(bounds.right, 0.0f);
  auto shader = tgfx::Shader::MakeLinearGradient(startPoint, endPoint, colors, {});
  paint.setShader(shader);
  canvas->concat(matrix);
  canvas->drawSimpleText(text, 0, 0, font, paint);
  paint.setShader(nullptr);
  paint.setColor({1.0f, 1.0f, 1.0f, 1.0f});
  paint.setStyle(tgfx::PaintStyle::Stroke);
  paint.setStrokeWidth(scale);
  canvas->drawSimpleText(text, 0, 0, font, paint);
}

}  // namespace tdraw
