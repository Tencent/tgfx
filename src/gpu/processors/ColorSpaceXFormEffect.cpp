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

#include "ColorSpaceXFormEffect.h"

namespace tgfx {

PlacementPtr<FragmentProcessor> ColorSpaceXformEffect::Make(BlockBuffer* buffer,
                                                            PlacementPtr<FragmentProcessor> child,
                                                            ColorSpace* src, AlphaType srcAT,
                                                            ColorSpace* dst, AlphaType dstAT) {
  return Make(buffer, std::move(child),
              std::make_shared<ColorSpaceXformSteps>(src, srcAT, dst, dstAT));
}

PlacementPtr<FragmentProcessor> ColorSpaceXformEffect::Make(
    BlockBuffer* buffer, PlacementPtr<FragmentProcessor> child,
    std::shared_ptr<ColorSpaceXformSteps> colorXform) {
  if(child == nullptr) {
    return nullptr;
  }
  return buffer->make<ColorSpaceXformEffect>(std::move(child), std::move(colorXform));
}

void ColorSpaceXformEffect::emitCode(EmitArgs& args) const {
  auto uniformHandler = args.uniformHandler;
  auto fragBuilder = args.fragBuilder;
  ColorSpaceXformHelper helper{};
  helper.emitCode(uniformHandler, colorSpaceXformSteps.get());
  std::string childColor;
  emitChild(0, args.inputColor.c_str(), &childColor, args);
  std::string outputColor;
  fragBuilder->appendColorGamutXform(&outputColor, childColor.c_str(), &helper);
  fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), outputColor.c_str());
}

void ColorSpaceXformEffect::onComputeProcessorKey(BytesKey* bytesKey) const {
  uint32_t key = ColorSpaceXformSteps::XFormKey(colorSpaceXformSteps.get());
  bytesKey->write(key);
}

ColorSpaceXformEffect::ColorSpaceXformEffect(PlacementPtr<FragmentProcessor> child,
                                             std::shared_ptr<ColorSpaceXformSteps> colorXform)
    : FragmentProcessor(classID()), colorSpaceXformSteps(std::move(colorXform)){
  registerChildProcessor(std::move(child));
}

void ColorSpaceXformEffect::onSetData(UniformData* /*vertexUniformData*/,
                                             UniformData* fragmentUniformData) const {
  ColorSpaceXformHelper helper{};
  helper.setData(fragmentUniformData, colorSpaceXformSteps.get());
}
}  // namespace tgfx