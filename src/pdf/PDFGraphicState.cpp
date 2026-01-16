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

#include "PDFGraphicState.h"
#include "pdf/PDFDocumentImpl.h"
#include "pdf/PDFTypes.h"
#include "pdf/PDFUtils.h"
#include "tgfx/core/BlendMode.h"

namespace tgfx {

namespace {

uint8_t FilterPDFBlendMode(BlendMode mode) {
  if (!PDFUtils::BlendModeName(mode) || BlendMode::Xor == mode || BlendMode::PlusDarker == mode ||
      BlendMode::PlusLighter == mode) {
    mode = BlendMode::SrcOver;
  }
  return static_cast<uint8_t>(mode);
}

}  // namespace

PDFIndirectReference PDFGraphicState::GetGraphicStateForPaint(PDFDocumentImpl* document,
                                                              const Brush& brush) {
  DEBUG_ASSERT(document);
  auto mode = brush.blendMode;

  PDFFillGraphicState fillKey = {brush.color.alpha, FilterPDFBlendMode(mode)};
  auto& fillMap = document->fillGSMap;
  auto iter = fillMap.find(fillKey);
  if (iter != fillMap.end()) {
    return iter->second;
  }

  PDFDictionary state;
  state.reserve(2);
  state.insertScalar("ca", fillKey.alpha);
  state.insertName("BM", PDFUtils::BlendModeName(static_cast<BlendMode>(fillKey.blendMode)));
  PDFIndirectReference reference = document->emit(state);
  fillMap[fillKey] = reference;
  return reference;
}

PDFIndirectReference PDFGraphicState::GetSMaskGraphicState(PDFIndirectReference sMask,
                                                           bool /*invert*/, SMaskMode sMaskMode,
                                                           PDFDocumentImpl* doc) {
  // The practical chances of using the same mask more than once are unlikely
  // enough that it's not worth canonicalizing.
  auto sMaskDict = PDFDictionary::Make("Mask");
  if (sMaskMode == SMaskMode::Alpha) {
    sMaskDict->insertName("S", "Alpha");
  } else if (sMaskMode == SMaskMode::Luminosity) {
    sMaskDict->insertName("S", "Luminosity");
  }
  sMaskDict->insertRef("G", sMask);
  auto result = PDFDictionary::Make("ExtGState");
  result->insertObject("SMask", std::move(sMaskDict));
  return doc->emit(*result);
}

}  // namespace tgfx
