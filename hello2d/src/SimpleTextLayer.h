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

#pragma once

#include "tgfx/layers/Layer.h"

namespace hello2d {
struct TextLine {
  float left = 0.f;
  float right = 0.f;
  float linePosition = 0.f;
};

class Element {
 public:
  enum class Type { Text, Image };

  std::string text = "";
  tgfx::Font font = {};
  std::vector<tgfx::Paint> paints = {};

  std::shared_ptr<tgfx::Image> image = nullptr;
  float width = 0.f;
  float height = 0.f;

  std::vector<std::pair<size_t, size_t>> underlineIndex = {};
  std::vector<std::pair<size_t, size_t>> deletelineIndex = {};
  Type type = Type::Text;

 private:
  tgfx::Rect imageRect = {};
  std::shared_ptr<tgfx::TextBlob> textBlob = nullptr;
  std::vector<TextLine> underline = {};
  std::vector<TextLine> deleteline = {};

  friend class SimpleTextLayer;
};

class SimpleTextLayer : public tgfx::Layer {
 public:
  static std::shared_ptr<SimpleTextLayer> Make();

  void setElements(std::vector<Element> value) {
    richTexts = std::move(value);
    invalidateContent();
  }

 protected:
  void onUpdateContent(tgfx::LayerRecorder* recorder) override;

 private:
  void updateLayout();

  std::vector<Element> richTexts = {};
};

}  // namespace hello2d
