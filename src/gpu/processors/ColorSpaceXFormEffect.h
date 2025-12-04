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
#include "FragmentProcessor.h"
#include "core/ColorSpaceXformSteps.h"
#include "gpu/ColorSpaceXformHelper.h"

namespace tgfx {

class ColorSpaceXformEffect : public FragmentProcessor {
 public:
  static PlacementPtr<FragmentProcessor> Make(BlockAllocator* allocator, ColorSpace* src,
                                              AlphaType srcAT, ColorSpace* dst, AlphaType dstAT);

  static PlacementPtr<FragmentProcessor> Make(BlockAllocator* allocator,
                                              std::shared_ptr<ColorSpaceXformSteps> colorXform);

  ColorSpaceXformEffect(std::shared_ptr<ColorSpaceXformSteps> colorXform);

  std::string name() const override {
    return "ColorSpaceXformEffect";
  }

  const ColorSpaceXformSteps* colorXform() const {
    return colorSpaceXformSteps.get();
  }

  void emitCode(EmitArgs& args) const override;

 private:
  DEFINE_PROCESSOR_CLASS_ID
  void onSetData(UniformData*, UniformData*) const override;
  void onComputeProcessorKey(BytesKey* bytesKey) const override;
  std::shared_ptr<ColorSpaceXformSteps> colorSpaceXformSteps;
};
}  // namespace tgfx