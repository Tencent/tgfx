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

#include "PDFGraphicStackState.h"
#include "PDFDocumentImpl.h"
#include "core/MCState.h"
#include "core/utils/Log.h"
#include "pdf/PDFUtils.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/PathTypes.h"
#include "tgfx/core/WriteStream.h"

namespace tgfx {

namespace {
void EmitPDFColor(Color color, const std::shared_ptr<WriteStream>& result) {
  DEBUG_ASSERT(color.alpha == 1);  // We handle alpha elsewhere.
  PDFUtils::AppendColorComponent(color.red, result);
  result->writeText(" ");
  PDFUtils::AppendColorComponent(color.green, result);
  result->writeText(" ");
  PDFUtils::AppendColorComponent(color.blue, result);
  result->writeText(" ");
}

void AppendClip(const MCState& state, const std::shared_ptr<MemoryWriteStream>& stream) {
  if (state.clip.isRect()) {
    auto bound = state.clip.getBounds();
    PDFUtils::AppendRectangle(bound, stream);
    stream->writeText("W* n\n");
  } else {
    PDFUtils::EmitPath(state.clip, false, stream);
    auto clipFill = state.clip.getFillType();
    if (clipFill == PathFillType::EvenOdd) {
      stream->writeText("W* n\n");
    } else {
      stream->writeText("W n\n");
    }
  }
}

}  // namespace

////////////////////////////////////////////////////////////////////////////////

void PDFGraphicStackState::updateClip(const MCState& state) {
  if (state.clip.isEmpty()) {
    return;
  }
  const auto& currentState = this->currentEntry()->state;
  if (currentState.clip.isSame(state.clip) && currentState.matrix == state.matrix) {
    return;
  }
  while (stackDepth > 0) {
    pop();
    if (currentState.clip.isSame(state.clip) && currentState.matrix == state.matrix) {
      return;
    }
  }

  push();
  this->currentEntry()->state = state;
  AppendClip(state, contentStream);
}

void PDFGraphicStackState::updateMatrix(const Matrix& matrix) {
  if (matrix == currentEntry()->matrix) {
    return;
  }

  if (!currentEntry()->matrix.isIdentity()) {
    DEBUG_ASSERT(stackDepth > 0);
    const auto& currentState = entries[stackDepth].state;
    const auto& previousState = entries[stackDepth - 1].state;

    ASSERT(currentState.clip.isSame(previousState.clip) &&
           currentState.matrix == previousState.matrix);
    pop();
    DEBUG_ASSERT(currentEntry()->matrix.isIdentity());
  }

  if (matrix.isIdentity()) {
    return;
  }

  push();
  PDFUtils::AppendTransform(matrix, contentStream);
  currentEntry()->matrix = matrix;
}

void PDFGraphicStackState::updateDrawingState(const PDFGraphicStackState::Entry& state) {
  // PDF treats a shader as a color, so we only set one or the other.
  if (state.shaderIndex >= 0) {
    if (state.shaderIndex != currentEntry()->shaderIndex) {
      PDFUtils::ApplyPattern(state.shaderIndex, contentStream);
      currentEntry()->shaderIndex = state.shaderIndex;
    }
  } else if (state.color != currentEntry()->color || currentEntry()->shaderIndex >= 0) {
    if (firstUpdateColor &&
        ColorSpace::Equals(currentEntry()->color.colorSpace.get(), state.color.colorSpace.get())) {
      contentStream->writeText("/DeviceRGB CS\n");
      contentStream->writeText("/DeviceRGB cs\n");
    } else if (!ColorSpace::Equals(currentEntry()->color.colorSpace.get(),
                                   state.color.colorSpace.get())) {
      if (state.color.colorSpace == nullptr) {
        contentStream->writeText("/DeviceRGB CS\n");
        contentStream->writeText("/DeviceRGB cs\n");
      } else {
        auto ref = document->emitColorSpace(state.color.colorSpace);
        colorSpaceResources->insert(ref);
        std::string command = "/C" + std::to_string(ref.value);
        contentStream->writeText(command + " CS\n");
        contentStream->writeText(command + " cs\n");
      }
    }
    if (firstUpdateColor) {
      firstUpdateColor = false;
    }
    EmitPDFColor(state.color, contentStream);
    contentStream->writeText("SC\n");
    EmitPDFColor(state.color, contentStream);
    contentStream->writeText("sc\n");
    currentEntry()->color = state.color;
    currentEntry()->shaderIndex = -1;
  }

  if (state.graphicStateIndex != currentEntry()->graphicStateIndex) {
    PDFUtils::ApplyGraphicState(state.graphicStateIndex, contentStream);
    currentEntry()->graphicStateIndex = state.graphicStateIndex;
  }

  if (state.textScaleX != 0) {
    if (state.textScaleX != currentEntry()->textScaleX) {
      float pdfScale = state.textScaleX * 100;
      PDFUtils::AppendFloat(pdfScale, contentStream);
      contentStream->writeText(" Tz\n");
      currentEntry()->textScaleX = state.textScaleX;
    }
  }
}

void PDFGraphicStackState::push() {
  DEBUG_ASSERT(stackDepth < MaxStackDepth);
  contentStream->writeText("q\n");
  ++stackDepth;
  entries[stackDepth] = entries[stackDepth - 1];
}

void PDFGraphicStackState::pop() {
  DEBUG_ASSERT(stackDepth > 0);
  contentStream->writeText("Q\n");
  entries[stackDepth] = PDFGraphicStackState::Entry();
  --stackDepth;
}

void PDFGraphicStackState::drainStack() {
  if (contentStream) {
    while (stackDepth) {
      this->pop();
    }
  }
  DEBUG_ASSERT(stackDepth == 0);
}

}  // namespace tgfx