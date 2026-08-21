/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {

enum class GlassUDFBlurDirection { Horizontal, Vertical };

/**
 * Selects which fields a tent pass produces. Refraction writes the 24-bit fine field into RGB;
 * EdgeLight writes the coarse field into A; Both writes both (the original combined layout).
 */
enum class GlassUDFField { Refraction, EdgeLight, Both };

/**
 * @description Blurs alpha coverage with tent kernels in a single pass and writes one or two
 * fields into one RGBA8 target: RGB carries the wide "fine" field with 24-bit precision for the
 * refraction gradient, A carries the narrow "coarse" field that drives the edge light width.
 *
 * The two source processors must sample the same image with the same coordinate matrix. Separate
 * instances are required because a single child cannot be emitted twice without redeclaring its
 * uniforms.
 */
class GlassUDFTentBlurFragmentProcessor : public FragmentProcessor {
 public:
  /**
   * @description Creates the processor. Both radii are expressed in destination texel units.
   * @param inputIsPacked True when the sources already produce this processor's own layout, which
   * is the case for the second (vertical) pass.
   */
  static PlacementPtr<FragmentProcessor> Make(BlockAllocator* allocator,
                                              PlacementPtr<FragmentProcessor> fineSource,
                                              PlacementPtr<FragmentProcessor> coarseSource,
                                              float fineRadius, float coarseRadius,
                                              GlassUDFBlurDirection direction, int maxRadius,
                                              bool inputIsPacked,
                                              GlassUDFField field = GlassUDFField::Both);

  std::string name() const override {
    return "GlassUDFTentBlurFragmentProcessor";
  }

  void emitCode(EmitArgs& args) const override;

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  GlassUDFTentBlurFragmentProcessor(PlacementPtr<FragmentProcessor> fineSource,
                                    PlacementPtr<FragmentProcessor> coarseSource, float fineRadius,
                                    float coarseRadius, GlassUDFBlurDirection direction,
                                    int maxRadius, bool inputIsPacked, GlassUDFField field);

  void onComputeProcessorKey(BytesKey* key) const override;

  void onSetData(UniformData* vertexUniformData, UniformData* fragmentUniformData) const override;

  float fineRadius = 0.0f;
  float coarseRadius = 0.0f;
  GlassUDFBlurDirection direction = GlassUDFBlurDirection::Horizontal;
  int maxRadius = 64;
  bool inputIsPacked = false;
  GlassUDFField field = GlassUDFField::Both;

  friend class BlockAllocator;
};

}  // namespace tgfx
