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
#include "ColorSpaceXformHelper.h"
#include <array>
#include "core/utils/Log.h"
namespace tgfx {

int TFTypeToIndex(gfx::skcms_TFType type) {
  switch (type) {
    case gfx::skcms_TFType_sRGBish:
      return 0;
    case gfx::skcms_TFType_PQish:
      return 1;
    case gfx::skcms_TFType_HLGish:
      return 2;
    case gfx::skcms_TFType_HLGinvish:
      return 3;
    default:
      return -1;
  }
}

bool ColorSpaceXformStepsSupported(const ColorSpaceXformSteps* steps) {
  if (steps == nullptr) {
    return false;
  }
  if (steps->flags.linearize &&
      TFTypeToIndex(gfx::skcms_TransferFunction_getType(
          reinterpret_cast<const gfx::skcms_TransferFunction*>(&steps->srcTransferFunction))) < 0) {
    return false;
  }
  if (steps->flags.encode && TFTypeToIndex(gfx::skcms_TransferFunction_getType(
                                 reinterpret_cast<const gfx::skcms_TransferFunction*>(
                                     &steps->dstTransferFunctionInverse))) < 0) {
    return false;
  }
  return true;
}

namespace {

// One color-space field: the addressing-neutral key the shared upload body writes through, so the
// name mode (prefix or per-slot arrays) and the element mode (a module's derived layout) run one
// and the same value packing.
enum class CSField {
  CSFlags,
  SrcTF0,
  SrcTF1,
  SrcTFType,
  SrcOOTF,
  ColorXform,
  DstOOTF,
  DstTF0,
  DstTF1,
  DstTFType,
};

ColorMatrix33 CSIdentityMatrix() {
  return {{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}};
}

// The std140 packing of a mat3 uniform: three vec4 columns.
std::array<float, 12> PackColorXform(const ColorMatrix33& matrix) {
  return {matrix.values[0][0], matrix.values[1][0], matrix.values[2][0], 0.0f,
          matrix.values[0][1], matrix.values[1][1], matrix.values[2][1], 0.0f,
          matrix.values[0][2], matrix.values[1][2], matrix.values[2][2], 0.0f};
}

// The element mode: one module's color-space block addressed by element index through the derived
// layout. The disabled steps still write their identity placeholders when the module declares the
// fields, matching the name mode's precompiled contract.
struct ElementSink {
  SlotWriter* writer;
  const ColorSpaceXformHelper::FieldElements& fields;

  uint8_t indexOf(CSField field) const;
  void optionalInt(CSField field, int value) const {
    writer->setIfPresent(indexOf(field), value);
  }
  void optionalFloat4(CSField field, const float (&value)[4]) const {
    writer->setIfPresent(indexOf(field), value);
  }
  void requiredFloat4(CSField field, const float (&value)[4]) const {
    writer->setIfPresent(indexOf(field), value);
  }
  void colorXform(const ColorSpaceXformSteps* steps, bool gamut) const {
    const auto& matrix = gamut ? steps->srcToDstMatrix : CSIdentityMatrix();
    writer->setIfPresent(indexOf(CSField::ColorXform), PackColorXform(matrix));
  }
};

uint8_t ElementSink::indexOf(CSField field) const {
  switch (field) {
    case CSField::CSFlags:
      return fields.csFlags;
    case CSField::SrcTF0:
      return fields.srcTF0;
    case CSField::SrcTF1:
      return fields.srcTF1;
    case CSField::SrcTFType:
      return fields.srcTFType;
    case CSField::SrcOOTF:
      return fields.srcOOTF;
    case CSField::ColorXform:
      return fields.colorXform;
    case CSField::DstOOTF:
      return fields.dstOOTF;
    case CSField::DstTF0:
      return fields.dstTF0;
    case CSField::DstTF1:
      return fields.dstTF1;
    case CSField::DstTFType:
      return fields.dstTFType;
  }
  return 0;
}

// The one upload body: the flags bitmask, each enabled step's real parameters (disabled steps
// write identity placeholders their flags bit keeps unread), and the gamut matrix or its identity
// stand-in.
template <typename Sink>
void UploadColorSpaceSteps(Sink& sink, const ColorSpaceXformSteps* colorSpaceXform) {
  const auto flags = colorSpaceXform->flags;

  static const float Zero4[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  sink.optionalInt(CSField::CSFlags, static_cast<int>(flags.mask()));

  if (flags.linearize) {
    float srcTF0[4] = {
        colorSpaceXform->srcTransferFunction.g, colorSpaceXform->srcTransferFunction.a,
        colorSpaceXform->srcTransferFunction.b, colorSpaceXform->srcTransferFunction.c};
    float srcTF1[4] = {colorSpaceXform->srcTransferFunction.d,
                       colorSpaceXform->srcTransferFunction.e,
                       colorSpaceXform->srcTransferFunction.f, 0.0f};
    sink.requiredFloat4(CSField::SrcTF0, srcTF0);
    sink.requiredFloat4(CSField::SrcTF1, srcTF1);
    int srcType = TFTypeToIndex(
        gfx::skcms_TransferFunction_getType(reinterpret_cast<const gfx::skcms_TransferFunction*>(
            &colorSpaceXform->srcTransferFunction)));
    if (srcType < 0) {
      // The matcher rejects unsupported transfer functions before a program is created, so
      // reaching here means an entry point bypassed that check. Report loudly instead of
      // silently rendering with the sRGB branch.
      LOGE(
          "ColorSpaceXformHelper: unsupported source transfer function; the draw reached the "
          "upload path without the matcher rejecting it");
    }
    sink.optionalInt(CSField::SrcTFType, srcType < 0 ? 0 : srcType);
  } else {
    sink.optionalFloat4(CSField::SrcTF0, Zero4);
    sink.optionalFloat4(CSField::SrcTF1, Zero4);
    sink.optionalInt(CSField::SrcTFType, 0);
  }

  if (flags.srcOOTF) {
    sink.requiredFloat4(CSField::SrcOOTF, colorSpaceXform->srcOOTF);
  } else {
    sink.optionalFloat4(CSField::SrcOOTF, Zero4);
  }

  sink.colorXform(colorSpaceXform, flags.gamutTransform);

  if (flags.dstOOTF) {
    sink.requiredFloat4(CSField::DstOOTF, colorSpaceXform->dstOOTF);
  } else {
    sink.optionalFloat4(CSField::DstOOTF, Zero4);
  }

  if (flags.encode) {
    float dstTF0[4] = {colorSpaceXform->dstTransferFunctionInverse.g,
                       colorSpaceXform->dstTransferFunctionInverse.a,
                       colorSpaceXform->dstTransferFunctionInverse.b,
                       colorSpaceXform->dstTransferFunctionInverse.c};
    float dstTF1[4] = {colorSpaceXform->dstTransferFunctionInverse.d,
                       colorSpaceXform->dstTransferFunctionInverse.e,
                       colorSpaceXform->dstTransferFunctionInverse.f, 0.0f};
    sink.requiredFloat4(CSField::DstTF0, dstTF0);
    sink.requiredFloat4(CSField::DstTF1, dstTF1);
    int dstType = TFTypeToIndex(
        gfx::skcms_TransferFunction_getType(reinterpret_cast<const gfx::skcms_TransferFunction*>(
            &colorSpaceXform->dstTransferFunctionInverse)));
    if (dstType < 0) {
      LOGE(
          "ColorSpaceXformHelper: unsupported destination transfer function; the draw reached "
          "the upload path without the matcher rejecting it");
    }
    sink.optionalInt(CSField::DstTFType, dstType < 0 ? 0 : dstType);
  } else {
    sink.optionalFloat4(CSField::DstTF0, Zero4);
    sink.optionalFloat4(CSField::DstTF1, Zero4);
    sink.optionalInt(CSField::DstTFType, 0);
  }
}

}  // namespace

void ColorSpaceXformHelper::setData(SlotWriter* writer, const FieldElements& fields,
                                    const ColorSpaceXformSteps* colorSpaceXform) {
  if (!colorSpaceXform) {
    return;
  }
  ElementSink sink = {writer, fields};
  UploadColorSpaceSteps(sink, colorSpaceXform);
}
}  // namespace tgfx
