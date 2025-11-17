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

#pragma once

#include <limits>
#include <memory>
#include <utility>
#include "core/MCState.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/WriteStream.h"
#include <unordered_set>

namespace tgfx {
class ColorSpaceConverter;
}

namespace tgfx {
struct PDFIndirectReference;
class PDFDocumentImpl;
struct PDFGraphicStackState {
  struct Entry {
    Matrix matrix = Matrix::I();
    MCState state;
    Color color = {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN(),
                   std::numeric_limits<float>::quiet_NaN(),
                   std::numeric_limits<float>::quiet_NaN()};
    float textScaleX = 1;  // Zero means we don't care what the value is.
    int shaderIndex = -1;
    int graphicStateIndex = -1;
  };

  // Must use stack for matrix, and for clip, plus one for no matrix or clip.
  static constexpr int MaxStackDepth = 2;
  Entry entries[MaxStackDepth + 1];
  int stackDepth = 0;
  std::shared_ptr<MemoryWriteStream> contentStream;
  PDFDocumentImpl* document;
  std::unordered_set<PDFIndirectReference>* colorSpaceResources;
  explicit PDFGraphicStackState(std::shared_ptr<MemoryWriteStream> stream = nullptr, PDFDocumentImpl* doc = nullptr, std::unordered_set<PDFIndirectReference>* colorSpaceResources = nullptr)
      : contentStream(std::move(stream)), document(std::move(doc)), colorSpaceResources(colorSpaceResources) {
  }

  void updateClip(const MCState& state);
  void updateMatrix(const Matrix& matrix);
  void updateDrawingState(const Entry& state);
  void push();
  void pop();
  void drainStack();
  Entry* currentEntry() {
    return &entries[stackDepth];
  }
};

}  // namespace tgfx