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

#include "Rasterizer.h"

namespace tgfx {
class TextRasterizer : public Rasterizer {
 public:
  TextRasterizer(std::shared_ptr<TextBlob> textBlob, const ISize& clipSize, const Matrix& matrix,
                 const Stroke* stroke)
      : Rasterizer(clipSize, matrix, stroke), textBlob(std::move(textBlob)) {
  }

 protected:
  void onRasterize(Mask* mask, const Stroke* stroke) const override {
    mask->fillText(textBlob.get(), stroke);
  }

 private:
  std::shared_ptr<TextBlob> textBlob = nullptr;
};

class PathRasterizer : public Rasterizer {
 public:
  PathRasterizer(Path path, const ISize& clipSize, const Matrix& matrix, const Stroke* stroke)
      : Rasterizer(clipSize, matrix, stroke), path(std::move(path)) {
  }

 protected:
  void onRasterize(Mask* mask, const Stroke* stroke) const override {
    mask->fillPath(path, stroke);
  }

 private:
  Path path = {};
};

std::shared_ptr<Rasterizer> Rasterizer::MakeFrom(std::shared_ptr<TextBlob> textBlob,
                                                 const ISize& clipSize, const Matrix& matrix,
                                                 const Stroke* stroke) {
  if (textBlob == nullptr || clipSize.isEmpty()) {
    return nullptr;
  }
  return std::make_shared<TextRasterizer>(std::move(textBlob), clipSize, matrix, stroke);
}

std::shared_ptr<Rasterizer> Rasterizer::MakeFrom(Path path, const ISize& clipSize,
                                                 const Matrix& matrix, const Stroke* stroke) {
  if (path.isEmpty() || clipSize.isEmpty()) {
    return nullptr;
  }
  return std::make_shared<PathRasterizer>(std::move(path), clipSize, matrix, stroke);
}

Rasterizer::Rasterizer(const ISize& clipSize, const Matrix& matrix, const Stroke* s)
    : ImageGenerator(clipSize.width, clipSize.height), matrix(matrix) {
  if (s != nullptr) {
    stroke = new Stroke(*s);
  }
}

Rasterizer::~Rasterizer() {
  delete stroke;
}

std::shared_ptr<ImageBuffer> Rasterizer::onMakeBuffer(bool tryHardware) const {
  auto mask = Mask::Make(width(), height(), tryHardware);
  if (!mask) {
    return nullptr;
  }
  mask->setMatrix(matrix);
  onRasterize(mask.get(), stroke);
  return mask->makeBuffer();
}
}  // namespace tgfx
