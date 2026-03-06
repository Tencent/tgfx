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

#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {
class AARectEffect : public FragmentProcessor {
 public:
  /**
   * Creates an AARectEffect that clips to the given rectangle.
   * @param allocator The allocator to use for memory allocation.
   * @param rect The rectangle to clip to, in device coordinates.
   * @param antiAlias If true, the clip edge will be anti-aliased.
   */
  static PlacementPtr<AARectEffect> Make(BlockAllocator* allocator, const Rect& rect,
                                         bool antiAlias = true);

  std::string name() const override {
    return "AARectEffect";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  AARectEffect(const Rect& rect, bool antiAlias)
      : FragmentProcessor(ClassID()), rect(rect), antiAlias(antiAlias) {
  }

  Rect rect = {};
  bool antiAlias = true;
};
}  // namespace tgfx
