/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <limits>
#include <memory>
#include <utility>
#include "core/MCState.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/WriteStream.h"

namespace tgfx {

struct PDFGraphicStackState {
  struct Entry {
    Matrix fMatrix = Matrix::I();
    uint32_t fClipStackGenID = 0;
    Color fColor = {
        std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN()};
    float fTextScaleX = 1;  // Zero means we don't care what the value is.
    int fShaderIndex = -1;
    int fGraphicStateIndex = -1;
  };

  // Must use stack for matrix, and for clip, plus one for no matrix or clip.
  inline static constexpr int MaxStackDepth = 2;
  Entry fEntries[MaxStackDepth + 1];
  int fStackDepth = 0;
  std::shared_ptr<MemoryWriteStream> fContentStream;

  explicit PDFGraphicStackState(std::shared_ptr<MemoryWriteStream> stream = nullptr)
      : fContentStream(std::move(stream)) {
  }

  void updateClip(const MCState& state, const Rect& bounds);
  void updateMatrix(const Matrix& matrix);
  void updateDrawingState(const Entry& state);
  void push();
  void pop();
  void drainStack();
  Entry* currentEntry() {
    return &fEntries[fStackDepth];
  }
};

}  // namespace tgfx