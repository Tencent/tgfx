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

#include <array>
#include <memory>
#include <optional>
#include "core/DrawContext.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Paint.h"

namespace tgfx {

class CanvasLayer {
 public:
  CanvasLayer(DrawContext* drawContext, const Paint* paint);

  DrawContext* drawContext = nullptr;
  std::unique_ptr<DrawContext> layerContext = nullptr;
  Paint layerPaint = {};
};

class CanvasState {
 public:
  CanvasState(const Matrix& matrix, const std::optional<std::array<double, 2>>& tileTranslation,
              std::unique_ptr<CanvasLayer> savedLayer = nullptr)
      : matrix(matrix), tileTranslation(tileTranslation), savedLayer(std::move(savedLayer)) {
  }

  Matrix matrix = Matrix::I();
  std::optional<std::array<double, 2>> tileTranslation = {};
  std::unique_ptr<CanvasLayer> savedLayer = nullptr;
};
}  // namespace tgfx
