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
#include "tgfx/core/BlendMode.h"

namespace tgfx {
class XfermodeFragmentProcessor : public FragmentProcessor {
 public:
  /**
   * The color input to the returned processor is treated as the src and the passed in processor is
   * the dst.
   */
  static PlacementPtr<FragmentProcessor> MakeFromDstProcessor(BlockAllocator* allocator,
                                                              PlacementPtr<FragmentProcessor> dst,
                                                              BlendMode mode);

  /**
   * The color input to the returned processor is treated as the dst and the passed in processor is
   * the src.
   */
  static PlacementPtr<FragmentProcessor> MakeFromSrcProcessor(BlockAllocator* allocator,
                                                              PlacementPtr<FragmentProcessor> src,
                                                              BlendMode mode);

  /**
   * Takes the input color, which is assumed to be unpremultiplied, passes it as an opaque color
   * to both src and dst. The outputs of a src and dst are blended using mode and the original
   * input's alpha is applied to the blended color to produce a premul output.
   */
  static PlacementPtr<FragmentProcessor> MakeFromTwoProcessors(BlockAllocator* allocator,
                                                               PlacementPtr<FragmentProcessor> src,
                                                               PlacementPtr<FragmentProcessor> dst,
                                                               BlendMode mode);

  std::string name() const override;

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  enum class Child { DstChild, SrcChild, TwoChild };

  XfermodeFragmentProcessor(PlacementPtr<FragmentProcessor> src,
                            PlacementPtr<FragmentProcessor> dst, BlendMode mode);

  Child child;
  BlendMode mode;
};
}  // namespace tgfx
