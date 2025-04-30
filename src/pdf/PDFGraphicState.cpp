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

#include "PDFGraphicState.h"
#include "pdf/PDFDocument.h"
#include "pdf/PDFTypes.h"
#include "pdf/PDFUtils.h"
#include "tgfx/core/BlendMode.h"

namespace tgfx {

namespace {

uint8_t filter_pdf_blend_mode(BlendMode mode) {
  if (!PDFUtils::BlendModeName(mode) || BlendMode::Xor == mode || BlendMode::PlusDarker == mode ||
      BlendMode::PlusLighter == mode) {
    mode = BlendMode::SrcOver;
  }
  return static_cast<uint8_t>(mode);
}

}  // namespace

PDFIndirectReference PDFGraphicState::GetGraphicStateForPaint(PDFDocument* document,
                                                              const Fill& fill) {
  DEBUG_ASSERT(document);
  auto mode = fill.blendMode;

  PDFFillGraphicState fillKey = {fill.color.alpha, filter_pdf_blend_mode(mode)};
  auto& fillMap = document->fFillGSMap;
  auto iter = fillMap.find(fillKey);
  if (iter != fillMap.end()) {
    return iter->second;
  }

  PDFDictionary state;
  state.reserve(2);
  state.insertScalar("ca", fillKey.fAlpha);
  // state.insertColorComponentF("ca", fillKey.fAlpha);
  state.insertName("BM", PDFUtils::BlendModeName(static_cast<BlendMode>(fillKey.fBlendMode)));
  PDFIndirectReference ref = document->emit(state);
  fillMap[fillKey] = ref;
  return ref;

  // if (SkPaint::kFill_Style == p.getStyle()) {
  //   SkPDFFillGraphicState fillKey = {p.getColor4f().fA, pdf_blend_mode(mode)};
  //   auto& fillMap = doc->fFillGSMap;
  //   if (SkPDFIndirectReference* statePtr = fillMap.find(fillKey)) {
  //     return *statePtr;
  //   }
  //   SkPDFDict state;
  //   state.reserve(2);
  //   state.insertColorComponentF("ca", fillKey.fAlpha);
  //   state.insertName("BM", as_pdf_blend_mode_name((SkBlendMode)fillKey.fBlendMode));
  //   SkPDFIndirectReference ref = doc->emit(state);
  //   fillMap.set(fillKey, ref);
  //   return ref;
  // } else {
  //   SkPDFStrokeGraphicState strokeKey = {p.getStrokeWidth(),        p.getStrokeMiter(),
  //                                        p.getColor4f().fA,         SkToU8(p.getStrokeCap()),
  //                                        SkToU8(p.getStrokeJoin()), pdf_blend_mode(mode)};
  //   auto& sMap = doc->fStrokeGSMap;
  //   if (SkPDFIndirectReference* statePtr = sMap.find(strokeKey)) {
  //     return *statePtr;
  //   }
  //   SkPDFDict state;
  //   state.reserve(8);
  //   state.insertColorComponentF("CA", strokeKey.fAlpha);
  //   state.insertColorComponentF("ca", strokeKey.fAlpha);
  //   state.insertInt("LC", to_stroke_cap(strokeKey.fStrokeCap));
  //   state.insertInt("LJ", to_stroke_join(strokeKey.fStrokeJoin));
  //   state.insertScalar("LW", strokeKey.fStrokeWidth);
  //   state.insertScalar("ML", strokeKey.fStrokeMiter);
  //   state.insertBool("SA", true);  // SA = Auto stroke adjustment.
  //   state.insertName("BM", as_pdf_blend_mode_name((SkBlendMode)strokeKey.fBlendMode));
  //   SkPDFIndirectReference ref = doc->emit(state);
  //   sMap.set(strokeKey, ref);
  //   return ref;
  // }
}

PDFIndirectReference PDFGraphicState::GetSMaskGraphicState(PDFIndirectReference sMask,
                                                           bool /*invert*/, SMaskMode sMaskMode,
                                                           PDFDocument* doc) {
  // The practical chances of using the same mask more than once are unlikely
  // enough that it's not worth canonicalizing.
  auto sMaskDict = PDFDictionary::Make("Mask");
  if (sMaskMode == SMaskMode::Alpha) {
    sMaskDict->insertName("S", "Alpha");
  } else if (sMaskMode == SMaskMode::Luminosity) {
    sMaskDict->insertName("S", "Luminosity");
  }
  sMaskDict->insertRef("G", sMask);
  // if (invert) {
  //   // let the doc deduplicate this object.
  //   if (doc->fInvertFunction == SkPDFIndirectReference()) {
  //     doc->fInvertFunction = make_invert_function(doc);
  //   }
  //   sMaskDict->insertRef("TR", doc->fInvertFunction);
  // }
  auto result = PDFDictionary::Make("ExtGState");
  result->insertObject("SMask", std::move(sMaskDict));
  return doc->emit(*result);
}

}  // namespace tgfx