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
#include <skcms.h>
#include <string>
#include <utility>
#include "UniformData.h"
#include "core/ColorSpaceXformSteps.h"
#include "gpu/shaders/ParamBuffer.h"

namespace tgfx {

/**
 * Maps an skcms transfer-function kind to the TFType uniform index the shaders switch on.
 * Returns -1 for kinds the precompiled math does not implement: the matcher treats that as
 * "this color space cannot ride the AOT path" (the draw falls back to runtime), and the upload
 * path treats it as a contract violation to report loudly — never as a silent downgrade to the
 * sRGB branch. This is the single authority for the mapping; do not re-derive it locally.
 */
int TFTypeToIndex(gfx::skcms_TFType type);

/**
 * Single admission gate for a color-space step set riding any precompiled path. Returns false
 * when an enabled linearize/encode step carries a transfer function whose kind the shared math
 * does not implement: the matcher already rejects such steps for the standalone effect, and this
 * gate extends the same rule to the owned-slot paths (tail / YUV / Perlin / chain) and the graph
 * builder, so an unsupported transfer function fails before a program is created instead of the
 * upload path logging and silently rendering with the sRGB branch.
 */
bool ColorSpaceXformStepsSupported(const ColorSpaceXformSteps* steps);

class SlotWriter;

/**
 * Uploads the uniforms of a color-space transform, in one of three addressing modes: bare names
 * (the standalone ColorSpaceXformEffect), prefix-composed names (the chain blocks), or per-slot
 * array elements. All uniform names are prefixed with namePrefix; an empty prefix yields the
 * plain names used by the standalone effect. When slotIndex >= 0 the plain names are written as
 * per-slot arrays instead, letting a multi-slot kernel give each slot an independent parameter
 * set (SrcTF0[0], SrcTF0[1], ...).
 */
class ColorSpaceXformHelper {
 public:
  ColorSpaceXformHelper() = default;

  explicit ColorSpaceXformHelper(std::string namePrefix) : prefix(std::move(namePrefix)) {
  }

  ColorSpaceXformHelper(int slotIndex, uint32_t) : slot(slotIndex) {
  }

  /**
   * The element indices of one module's color-space block (the Chain and YUVChainTail spellings):
   * the element mode addresses the fields through the module's derived layout, so no name is
   * composed per draw. A field the module gates out is simply skipped.
   */
  struct FieldElements {
    uint8_t csFlags = 0;
    uint8_t srcTF0 = 0;
    uint8_t srcTF1 = 0;
    uint8_t srcTFType = 0;
    uint8_t srcOOTF = 0;
    uint8_t colorXform = 0;
    uint8_t dstOOTF = 0;
    uint8_t dstTF0 = 0;
    uint8_t dstTF1 = 0;
    uint8_t dstTFType = 0;
  };

  void setData(UniformData* uniformData, const ColorSpaceXformSteps* colorSpaceXform);

  void setData(SlotWriter* writer, const FieldElements& fields,
               const ColorSpaceXformSteps* colorSpaceXform);

  bool applyUnpremul() const {
    return flags.unPremul;
  }
  bool applySrcTF() const {
    return flags.linearize;
  }
  bool applySrcOOTF() const {
    return flags.srcOOTF;
  }
  bool applyGamutXform() const {
    return flags.gamutTransform;
  }
  bool applyDstOOTF() const {
    return flags.dstOOTF;
  }
  bool applyDstTF() const {
    return flags.encode;
  }
  bool applyPremul() const {
    return flags.premul;
  }

 private:
  std::string prefix;
  int slot = -1;
  ColorSpaceXformSteps::Flags flags;
};
}  // namespace tgfx
