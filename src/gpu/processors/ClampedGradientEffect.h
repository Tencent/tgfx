/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include <climits>
#include "gpu/processors/FragmentProcessor.h"
#include "tgfx/core/Color.h"

namespace tgfx {
class ClampedGradientEffect : public FragmentProcessor {
 public:
  DEFINE_PROCESSOR_CLASS_ID
  static PlacementPtr<ClampedGradientEffect> Make(BlockAllocator* allocator,
                                                  PlacementPtr<FragmentProcessor> colorizer,
                                                  PlacementPtr<FragmentProcessor> gradLayout,
                                                  Color leftBorderColor, Color rightBorderColor);

  std::string name() const override {
    return "ClampedGradientEffect";
  }

  bool lowerToAOT(AOTNodeBuilder* builder, AOTNodeID input, AOTNodeID* output) const override;

  /** The colors the effect clamps to outside the gradient's [0, 1] span. */
  const Color& getLeftBorderColor() const {
    return leftBorderColor;
  }

  const Color& getRightBorderColor() const {
    return rightBorderColor;
  }

  /** The layout child whose kind selects the kernel's LayoutType branch. */
  const FragmentProcessor* getGradLayout() const {
    return childProcessor(gradLayoutIndex);
  }

  /** The kernel's LayoutType index for a layout processor's name (0 linear, 1 radial, 2 conic,
   *  3 diamond). Shared with the runtime writer so the mapping lives in one place. */
  static int LayoutTypeIndex(const std::string& layoutName);

 protected:
  ClampedGradientEffect(PlacementPtr<FragmentProcessor> colorizer,
                        PlacementPtr<FragmentProcessor> gradLayout, Color leftBorderColor,
                        Color rightBorderColor);

  size_t colorizerIndex = ULONG_MAX;
  size_t gradLayoutIndex = ULONG_MAX;
  Color leftBorderColor;
  Color rightBorderColor;
};
}  // namespace tgfx
