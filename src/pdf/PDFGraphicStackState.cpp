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

#include "PDFGraphicStackState.h"
#include <memory>
#include "core/utils/Log.h"
#include "pdf/PDFUtils.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/WriteStream.h"

namespace tgfx {

namespace {
void emit_pdf_color(Color color, const std::shared_ptr<WriteStream>& result) {
  DEBUG_ASSERT(color.alpha == 1);  // We handle alpha elsewhere.
  PDFUtils::AppendColorComponent(color.red, result);
  result->writeText(" ");
  PDFUtils::AppendColorComponent(color.green, result);
  result->writeText(" ");
  PDFUtils::AppendColorComponent(color.blue, result);
  result->writeText(" ");
}

// static SkRect rect_intersect(SkRect u, SkRect v) {
//   if (u.isEmpty() || v.isEmpty()) {
//     return {0, 0, 0, 0};
//   }
//   return u.intersect(v) ? u : SkRect{0, 0, 0, 0};
// }

// // Test to see if the clipstack is a simple rect, If so, we can avoid all PathOps code
// // and speed thing up.
// static bool is_rect(const SkClipStack& clipStack, const SkRect& bounds, SkRect* dst) {
//   SkRect currentClip = bounds;
//   SkClipStack::Iter iter(clipStack, SkClipStack::Iter::kBottom_IterStart);
//   while (const SkClipStack::Element* element = iter.next()) {
//     SkRect elementRect{0, 0, 0, 0};
//     switch (element->getDeviceSpaceType()) {
//       case SkClipStack::Element::DeviceSpaceType::kEmpty:
//         break;
//       case SkClipStack::Element::DeviceSpaceType::kRect:
//         elementRect = element->getDeviceSpaceRect();
//         break;
//       default:
//         return false;
//     }
//     if (element->isReplaceOp()) {
//       currentClip = rect_intersect(bounds, elementRect);
//     } else if (element->getOp() == SkClipOp::kIntersect) {
//       currentClip = rect_intersect(currentClip, elementRect);
//     } else {
//       return false;
//     }
//   }
//   *dst = currentClip;
//   return true;
// }

// // TODO: When there's no expanding clip ops, this function may not be necessary anymore.
// static bool is_complex_clip(const SkClipStack& stack) {
//   SkClipStack::Iter iter(stack, SkClipStack::Iter::kBottom_IterStart);
//   while (const SkClipStack::Element* element = iter.next()) {
//     if (element->isReplaceOp()) {
//       return true;
//     }
//   }
//   return false;
// }

// template <typename F>
// static void apply_clip(const SkClipStack& stack, const SkRect& outerBounds, F fn) {
//   // assumes clipstack is not complex.
//   constexpr SkRect kHuge{-30000, -30000, 30000, 30000};
//   SkClipStack::Iter iter(stack, SkClipStack::Iter::kBottom_IterStart);
//   SkRect bounds = outerBounds;
//   while (const SkClipStack::Element* element = iter.next()) {
//     SkPath operand;
//     element->asDeviceSpacePath(&operand);
//     SkPathOp op;
//     switch (element->getOp()) {
//       case SkClipOp::kDifference:
//         op = kDifference_SkPathOp;
//         break;
//       case SkClipOp::kIntersect:
//         op = kIntersect_SkPathOp;
//         break;
//     }
//     if (op == kDifference_SkPathOp || operand.isInverseFillType() ||
//         !kHuge.contains(operand.getBounds())) {
//       Op(SkPath::Rect(bounds), operand, op, &operand);
//     }
//     SkASSERT(!operand.isInverseFillType());
//     fn(operand);
//     if (!bounds.intersect(operand.getBounds())) {
//       return;  // return early;
//     }
//   }
// }

// static void append_clip_path(const SkPath& clipPath, SkWStream* wStream) {
//   SkPDFUtils::EmitPath(clipPath, SkPaint::kFill_Style, wStream);
//   SkPathFillType clipFill = clipPath.getFillType();
//   NOT_IMPLEMENTED(clipFill == SkPathFillType::kInverseEvenOdd, false);
//   NOT_IMPLEMENTED(clipFill == SkPathFillType::kInverseWinding, false);
//   if (clipFill == SkPathFillType::kEvenOdd) {
//     wStream->writeText("W* n\n");
//   } else {
//     wStream->writeText("W n\n");
//   }
// }

// static void append_clip(const SkClipStack& clipStack, const SkIRect& bounds, SkWStream* wStream) {
//   // The bounds are slightly outset to ensure this is correct in the
//   // face of floating-point accuracy and possible SkRegion bitmap
//   // approximations.
//   SkRect outsetBounds = SkRect::Make(bounds.makeOutset(1, 1));

//   SkRect clipStackRect;
//   if (is_rect(clipStack, outsetBounds, &clipStackRect)) {
//     SkPDFUtils::AppendRectangle(clipStackRect, wStream);
//     wStream->writeText("W* n\n");
//     return;
//   }

//   if (is_complex_clip(clipStack)) {
//     SkPath clipPath;
//     SkClipStack_AsPath(clipStack, &clipPath);
//     if (Op(clipPath, SkPath::Rect(outsetBounds), kIntersect_SkPathOp, &clipPath)) {
//       append_clip_path(clipPath, wStream);
//     }
//     // If Op() fails (pathological case; e.g. input values are
//     // extremely large or NaN), emit no clip at all.
//   } else {
//     apply_clip(clipStack, outsetBounds,
//                [wStream](const SkPath& path) { append_clip_path(path, wStream); });
//   }
// }
}  // namespace

////////////////////////////////////////////////////////////////////////////////

void PDFGraphicStackState::updateClip(const MCState& /*state*/, const Rect& /*bounds*/) {
  // uint32_t clipStackGenID = clipStack ? clipStack->getTopmostGenID() : SkClipStack::kWideOpenGenID;
  // if (clipStackGenID == currentEntry()->fClipStackGenID) {
  //   return;
  // }
  // while (fStackDepth > 0) {
  //   this->pop();
  //   if (clipStackGenID == currentEntry()->fClipStackGenID) {
  //     return;
  //   }
  // }
  // SkASSERT(currentEntry()->fClipStackGenID == SkClipStack::kWideOpenGenID);
  // if (clipStackGenID != SkClipStack::kWideOpenGenID) {
  //   SkASSERT(clipStack);
  //   this->push();

  //   currentEntry()->fClipStackGenID = clipStackGenID;
  //   append_clip(*clipStack, bounds, fContentStream);
  // }
}

void PDFGraphicStackState::updateMatrix(const Matrix& matrix) {
  if (matrix == currentEntry()->fMatrix) {
    return;
  }

  if (!currentEntry()->fMatrix.isIdentity()) {
    DEBUG_ASSERT(fStackDepth > 0);
    DEBUG_ASSERT(fEntries[fStackDepth].fClipStackGenID ==
                 fEntries[fStackDepth - 1].fClipStackGenID);
    this->pop();
    DEBUG_ASSERT(currentEntry()->fMatrix.isIdentity());
  }

  if (matrix.isIdentity()) {
    return;
  }

  this->push();
  PDFUtils::AppendTransform(matrix, fContentStream);
  currentEntry()->fMatrix = matrix;
}

void PDFGraphicStackState::updateDrawingState(const PDFGraphicStackState::Entry& state) {
  // PDF treats a shader as a color, so we only set one or the other.
  if (state.fShaderIndex >= 0) {
    if (state.fShaderIndex != currentEntry()->fShaderIndex) {
      PDFUtils::ApplyPattern(state.fShaderIndex, fContentStream);
      currentEntry()->fShaderIndex = state.fShaderIndex;
    }
  } else if (state.fColor != currentEntry()->fColor || currentEntry()->fShaderIndex >= 0) {
    emit_pdf_color(state.fColor, fContentStream);
    fContentStream->writeText("RG ");
    emit_pdf_color(state.fColor, fContentStream);
    fContentStream->writeText("rg\n");
    currentEntry()->fColor = state.fColor;
    currentEntry()->fShaderIndex = -1;
  }

  if (state.fGraphicStateIndex != currentEntry()->fGraphicStateIndex) {
    PDFUtils::ApplyGraphicState(state.fGraphicStateIndex, fContentStream);
    currentEntry()->fGraphicStateIndex = state.fGraphicStateIndex;
  }

  if (state.fTextScaleX != 0) {
    if (state.fTextScaleX != currentEntry()->fTextScaleX) {
      float pdfScale = state.fTextScaleX * 100;
      PDFUtils::AppendFloat(pdfScale, fContentStream);
      fContentStream->writeText(" Tz\n");
      currentEntry()->fTextScaleX = state.fTextScaleX;
    }
  }
}

void PDFGraphicStackState::push() {
  DEBUG_ASSERT(fStackDepth < MaxStackDepth);
  fContentStream->writeText("q\n");
  ++fStackDepth;
  fEntries[fStackDepth] = fEntries[fStackDepth - 1];
}

void PDFGraphicStackState::pop() {
  DEBUG_ASSERT(fStackDepth > 0);
  fContentStream->writeText("Q\n");
  fEntries[fStackDepth] = PDFGraphicStackState::Entry();
  --fStackDepth;
}

void PDFGraphicStackState::drainStack() {
  if (fContentStream) {
    while (fStackDepth) {
      this->pop();
    }
  }
  DEBUG_ASSERT(fStackDepth == 0);
}

}  // namespace tgfx