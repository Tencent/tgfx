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

#include "GeneratedOperators.h"
#include "gpu/ir/ProgramIREmit.h"
#include "GLSLXfermodeFragmentProcessor.h"
#include "gpu/glsl/GLSLBlend.h"
#include "gpu/processors/ConstColorProcessor.h"

namespace tgfx {
PlacementPtr<FragmentProcessor> XfermodeFragmentProcessor::MakeFromTwoProcessors(
    BlockAllocator* allocator, PlacementPtr<FragmentProcessor> src,
    PlacementPtr<FragmentProcessor> dst, BlendMode mode) {
  if (src == nullptr && dst == nullptr) {
    return nullptr;
  }
  switch (mode) {
    case BlendMode::Clear:
      return ConstColorProcessor::Make(allocator, PMColor::Transparent(), InputMode::Ignore);
    case BlendMode::Src:
      return src;
    case BlendMode::Dst:
      return dst;
    default:
      return allocator->make<GLSLXfermodeFragmentProcessor>(std::move(src), std::move(dst), mode);
  }
}

GLSLXfermodeFragmentProcessor::GLSLXfermodeFragmentProcessor(PlacementPtr<FragmentProcessor> src,
                                                             PlacementPtr<FragmentProcessor> dst,
                                                             BlendMode mode)
    : XfermodeFragmentProcessor(std::move(src), std::move(dst), mode) {
}

void GLSLXfermodeFragmentProcessor::emitCode(EmitArgs& args) const {
  // The IR route is the only route: the blend mode is a processor key constant, so it is
  // resolved at program build time — the emitted call names the one mode function this draw
  // uses, and the shader carries no runtime mode dispatch at all.
  if (args.irBuilder == nullptr) {
    return;
  }
  auto ir = args.irBuilder;
  auto uniformHandler = args.uniformHandler;
  auto inputOrWhite = [&]() -> ValueID {
    if (args.inputValue != INVALID_VALUE_ID) {
      return args.inputValue;
    }
    const auto one = ir->CreateConstant(1.0f);
    return ir->CreateConstruct(SLType::Float4, {one, one, one, one});
  };
#define EMIT_XP_CASE(name)                                                                        \
  case BlendMode::name:                                                                           \
    return XpBlend##name##Op::Emit(ir, uniformHandler, nullptr, src, dst, dstSrcAlpha, srcDstAlpha);
  auto emitBlend = [&](BlendMode blendMode, ValueID src, ValueID dst) -> ValueID {
    const auto srcAlpha = ir->CreateSwizzle(src, SLType::Float4, "aaaa");
    const auto dstAlpha = ir->CreateSwizzle(dst, SLType::Float4, "aaaa");
    const auto dstSrcAlpha = ir->CreateBinary(IRBinaryOp::Mul, SLType::Float4, dst, srcAlpha);
    const auto srcDstAlpha = ir->CreateBinary(IRBinaryOp::Mul, SLType::Float4, src, dstAlpha);
    switch (blendMode) {
    EMIT_XP_CASE(Clear)
    EMIT_XP_CASE(Src)
    EMIT_XP_CASE(Dst)
    EMIT_XP_CASE(SrcOver)
    EMIT_XP_CASE(DstOver)
    EMIT_XP_CASE(SrcIn)
    EMIT_XP_CASE(DstIn)
    EMIT_XP_CASE(SrcOut)
    EMIT_XP_CASE(DstOut)
    EMIT_XP_CASE(SrcATop)
    EMIT_XP_CASE(DstATop)
    EMIT_XP_CASE(Xor)
    EMIT_XP_CASE(PlusLighter)
    EMIT_XP_CASE(Modulate)
    EMIT_XP_CASE(Screen)
    EMIT_XP_CASE(Overlay)
    EMIT_XP_CASE(Darken)
    EMIT_XP_CASE(Lighten)
    EMIT_XP_CASE(ColorDodge)
    EMIT_XP_CASE(ColorBurn)
    EMIT_XP_CASE(HardLight)
    EMIT_XP_CASE(SoftLight)
    EMIT_XP_CASE(Difference)
    EMIT_XP_CASE(Exclusion)
    EMIT_XP_CASE(Multiply)
    EMIT_XP_CASE(Hue)
    EMIT_XP_CASE(Saturation)
    EMIT_XP_CASE(Color)
    EMIT_XP_CASE(Luminosity)
    EMIT_XP_CASE(PlusDarker)
    }
    return INVALID_VALUE_ID;
  };
#undef EMIT_XP_CASE
  if (child == XfermodeFragmentProcessor::Child::TwoChild) {
    // inputColor opaque: vec4(input.rgb, 1.0).
    const auto input = inputOrWhite();
    const auto rgb = ir->CreateSwizzle(input, SLType::Float3, "rgb");
    const auto one = ir->CreateConstant(1.0f);
    const auto opaque = ir->CreateConstruct(SLType::Float4, {rgb, one});
    std::string srcName = "xfer_src";
    std::string dstName = "xfer_dst";
    const auto srcValue = emitChildIR(0, "", &srcName, args, opaque);
    const auto dstValue = emitChildIR(1, "", &dstName, args, opaque);
    const auto blended = emitBlend(mode, srcValue, dstValue);
    // The epilogue re-multiplies through the input's alpha (the text tail's tgfxMulAlpha).
    const auto result = ir->CreateCall(OperatorID::MulAlpha, SLType::Float4,
                                       {blended, input}, args.outputColor);
    ir->StoreOutput(args.outputValue, result);
    return;
  }
  std::string childColor = "child";
  const auto input = inputOrWhite();
  // The single-child form hands the child the opaque input too (the text route's
  // vec4(input.rgb, 1.0)): the child's own epilogue modulates through that input's alpha.
  const auto childInput = [&]() -> ValueID {
    const auto rgb = ir->CreateSwizzle(input, SLType::Float3, "rgb");
    const auto one = ir->CreateConstant(1.0f);
    return ir->CreateConstruct(SLType::Float4, {rgb, one});
  };
  const auto childValue = emitChildIR(0, "", &childColor, args, childInput());
  // The single-child form blends the child against the input; which side is src depends on the
  // child kind (the text route's argument order).
  const auto src = child == XfermodeFragmentProcessor::Child::DstChild ? input : childValue;
  const auto dst = child == XfermodeFragmentProcessor::Child::DstChild ? childValue : input;
  const auto blended = emitBlend(mode, src, dst);
  ir->StoreOutput(args.outputValue, blended);
}
}  // namespace tgfx
