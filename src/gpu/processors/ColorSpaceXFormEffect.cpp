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
#include "gpu/ColorSpaceXformHelper.h"

namespace tgfx {

PlacementPtr<FragmentProcessor> ColorSpaceXformEffect::Make(BlockAllocator* allocator,
                                                            ColorSpace* src, AlphaType srcAT,
                                                            ColorSpace* dst, AlphaType dstAT) {
  return Make(allocator, std::make_shared<ColorSpaceXformSteps>(src, srcAT, dst, dstAT));
}

PlacementPtr<FragmentProcessor> ColorSpaceXformEffect::Make(
    BlockAllocator* allocator, std::shared_ptr<ColorSpaceXformSteps> colorXform) {
  return allocator->make<ColorSpaceXformEffect>(std::move(colorXform));
}



void ColorSpaceXformEffect::emitCode(EmitArgs& args) const {
  auto uniformHandler = args.uniformHandler;
  auto fragBuilder = args.fragBuilder;
  ColorSpaceXformHelper helper{};
  helper.emitCode(uniformHandler, colorSpaceXformSteps.get());
  if (helper.isNoop()) {
    fragBuilder->copyValue(args.outputColor, args.inputColor);
    return;
  }
  uint32_t flags = helper.flagsMask();
  int srcTFType = TFTypeToIndex(helper.srcTFType());
  int dstTFType = TFTypeToIndex(helper.dstTFType());
  // A transfer-function family the shared curve does not implement takes the identity arm: its
  // stage leaves the flags, matching the no-op form the inline emission produced for the kind.
  if (srcTFType < 0) {
    flags &= ~2u;
    srcTFType = 0;
  }
  if (dstTFType < 0) {
    flags &= ~8u;
    dstTFType = 0;
  }
  auto entry = fragBuilder->appendFunction("chainApplyColorSpaceXform");
  if (entry.empty()) {
    return;
  }
  const std::string srcTF0 = helper.applySrcTF() ? helper.srcTFUniform0() : "vec4(0.0)";
  const std::string srcTF1 = helper.applySrcTF() ? helper.srcTFUniform1() : "vec4(0.0)";
  const std::string srcOOTF = helper.applySrcOOTF() ? helper.srcOOTFUniform() : "vec4(0.0)";
  const std::string colorXform =
      helper.applyGamutXform() ? helper.gamutXformUniform() : "mat3(1.0)";
  const std::string dstOOTF = helper.applyDstOOTF() ? helper.dstOOTFUniform() : "vec4(0.0)";
  const std::string dstTF0 = helper.applyDstTF() ? helper.dstTFUniform0() : "vec4(0.0)";
  const std::string dstTF1 = helper.applyDstTF() ? helper.dstTFUniform1() : "vec4(0.0)";
  fragBuilder->appendCall(
      args.outputColor, entry,
      {args.inputColor, std::to_string(flags), srcTF0, srcTF1, std::to_string(srcTFType), srcOOTF,
       colorXform, dstOOTF, dstTF0, dstTF1, std::to_string(dstTFType)});
}

void ColorSpaceXformEffect::onSetData(UniformData* /*vertexUniformData*/,
                                      UniformData* fragmentUniformData) const {
  ColorSpaceXformHelper helper{};
  helper.setData(fragmentUniformData, colorSpaceXformSteps.get());
}

ColorSpaceXformEffect::ColorSpaceXformEffect(std::shared_ptr<ColorSpaceXformSteps> colorXform)
    : FragmentProcessor(ClassID()), colorSpaceXformSteps(std::move(colorXform)) {
}

void ColorSpaceXformEffect::onComputeProcessorKey(BytesKey* bytesKey) const {
  uint32_t key = ColorSpaceXformSteps::XFormKey(colorSpaceXformSteps.get());
  bytesKey->write(key);
}

}  // namespace tgfx
