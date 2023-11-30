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

namespace drawers {
void SimpleText::onDraw(tgfx::Canvas* canvas, const drawers::AppHost* host) const {
  auto scale = host->density();
  auto width = static_cast<float>(host->width());
  auto height = static_cast<float>(host->height());
  auto screenWidth = width - (100.0f * scale);
  screenWidth = std::max(screenWidth, 300.0f);

  std::string text = "HelloTGFX";
  auto typeface = host->getTypeface("default");
  tgfx::Font font(typeface, 40 * scale);
  font.setFauxBold(true);
  auto textBlob = tgfx::TextBlob::MakeFrom(text, font);
  auto bounds = textBlob->getBounds();
  auto textScale = screenWidth / bounds.width();
  auto matrix =
      tgfx::Matrix::MakeTrans((width - bounds.width()) / 2, height / 2 - bounds.bottom * 1.2f);
  matrix.postScale(textScale, textScale, width / 2, height / 2);
  auto oldMatrix = canvas->getMatrix();
  canvas->concat(matrix);
  tgfx::Paint paint = {};
  paint.setColor({1.0f, 1.0f, 1.0f, 1.0f});
  paint.setStyle(tgfx::PaintStyle::Stroke);
  paint.setStrokeWidth(2 * scale);
  canvas->drawSimpleText(text, 0, 0, font, paint);
  paint.setStyle(tgfx::PaintStyle::Fill);
  tgfx::Color cyan = {0.0f, 1.0f, 1.0f, 1.0f};
  tgfx::Color magenta = {1.0f, 0.0f, 1.0f, 1.0f};
  tgfx::Color yellow = {1.0f, 1.0f, 0.0f, 1.0f};
  auto startPoint = tgfx::Point::Make(0.0f, 0.0f);
  auto endPoint = tgfx::Point::Make(screenWidth, 0.0f);
  auto shader = tgfx::Shader::MakeLinearGradient(startPoint, endPoint, {cyan, magenta, yellow}, {});
  paint.setShader(shader);
  canvas->drawSimpleText(text, 0, 0, font, paint);
  canvas->setMatrix(oldMatrix);

  std::string emojis = "🤡👻🐠🤩😃🤪🙈🙊🐒";
  typeface = host->getTypeface("emoji");
  font.setTypeface(typeface);
  textBlob = tgfx::TextBlob::MakeFrom(emojis, font);
  bounds = textBlob->getBounds();
  textScale = screenWidth / bounds.width();
  matrix = tgfx::Matrix::MakeTrans((width - bounds.width()) / 2, height / 2 - bounds.top * 1.2f);
  matrix.postScale(textScale, textScale, width / 2, height / 2);
  canvas->concat(matrix);
  canvas->drawSimpleText(emojis, 0, 0, font, {});
}

}  // namespace drawers
