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
#include "GLSLPorterDuffXferProcessor.h"
#include "gpu/glsl/GLSLOperatorEmitter.h"
#include "gpu/glsl/GLSLBlend.h"
#include "gpu/ir/ProgramIRBuilder.h"

namespace tgfx {
PlacementPtr<PorterDuffXferProcessor> PorterDuffXferProcessor::Make(BlockAllocator* allocator,
                                                                    BlendMode blend,
                                                                    DstTextureInfo dstTextureInfo) {
  return allocator->make<GLSLPorterDuffXferProcessor>(blend, std::move(dstTextureInfo));
}

GLSLPorterDuffXferProcessor::GLSLPorterDuffXferProcessor(BlendMode blend,
                                                         DstTextureInfo dstTextureInfo)
    : PorterDuffXferProcessor(blend, std::move(dstTextureInfo)) {
}

void GLSLPorterDuffXferProcessor::emitCode(const EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  auto uniformHandler = args.uniformHandler;
  const auto& dstColor = fragBuilder->dstColor();
  if (args.irBuilder != nullptr && args.inputColorValue != INVALID_VALUE_ID &&
      args.inputCoverageValue != INVALID_VALUE_ID && args.outputValue != INVALID_VALUE_ID) {
    auto ir = args.irBuilder;
    // The IR route carries the whole blend: the named chain values, the coefficient-mode primary
    // output transform, the destination read (a FragCoord-offset sample of the dst copy), the
    // mode's XpBlend operator, and — for the non-coefficient modes — the coverage mix against the
    // destination. The text tail below stays for the dual-source coefficient modes.
    auto resolveName = [&](const std::string& name) -> ValueID {
      if (name.empty() || name == "vec4(1.0)" || name == "1.0") {
        const auto one = ir->CreateConstant(1.0f);
        return ir->CreateConstruct(SLType::Float4, {one, one, one, one});
      }
      if (name == "vec4(0.0)" || name == "0.0") {
        const auto zero = ir->CreateConstant(0.0f);
        return ir->CreateConstruct(SLType::Float4, {zero, zero, zero, zero});
      }
      return ir->InternValue(name, SLType::Float4, Precision::High, IRValueKind::Local);
    };
    const auto srcValue = resolveName(args.inputColor);
    const auto covValue = resolveName(args.inputCoverage);
    if (srcValue == INVALID_VALUE_ID || covValue == INVALID_VALUE_ID) {
      ir->MarkFailed("the porter-duff chain values have no IR form");
      return;
    }
    // The text route's zero-coverage early-out: it discards on the fragment chain's tail, so the
    // discard source is that tail when the builder records one.
    ir->CreateDiscard(args.chainTailValue != INVALID_VALUE_ID ? args.chainTailValue : covValue);
    ValueID dstValue = INVALID_VALUE_ID;
    if (args.dstTextureSamplerHandle.isValid()) {
      const auto dstSampler = args.samplerValue(args.dstTextureSamplerHandle);
      const auto swizzleCode = ShaderBuilder::ReadSwizzleCode(
          uniformHandler->getSamplerSwizzle(args.dstTextureSamplerHandle));
      if (dstSampler == INVALID_VALUE_ID || swizzleCode < 0) {
        ir->MarkFailed("the porter-duff dst sampler has no IR value");
        return;
      }
      const auto upperLeftName = uniformHandler->addUniform("DstTextureUpperLeft",
                                                            UniformFormat::Float2,
                                                            ShaderStage::Fragment);
      const auto coordScaleName = uniformHandler->addUniform(
          "DstTextureCoordScale", UniformFormat::Float2, ShaderStage::Fragment);
      const auto upperLeft =
          ir->InternValue(upperLeftName, SLType::Float2, Precision::High, IRValueKind::Uniform);
      const auto coordScale = ir->InternValue(coordScaleName, SLType::Float2, Precision::High,
                                              IRValueKind::Uniform);
      const auto fragCoord = ir->CreateBuiltin("gl_FragCoord", SLType::Float4);
      const auto fragXY = ir->CreateSwizzle(fragCoord, SLType::Float2, "xy");
      const auto shifted =
          ir->CreateBinary(IRBinaryOp::Sub, SLType::Float2, fragXY, upperLeft);
      const auto dstCoord =
          ir->CreateBinary(IRBinaryOp::Mul, SLType::Float2, shifted, coordScale);
      dstValue = ir->CreateTextureSample(dstSampler, dstCoord, SLType::Float4, "dstRead",
                                         SLType::Texture2DSampler, swizzleCode);
    } else {
      const auto one = ir->CreateConstant(1.0f);
      dstValue = ir->CreateConstruct(SLType::Float4, {one, one, one, one});
    }
    // The coefficient modes follow the BlendFormula fields exactly: the primary-output
    // transform, the src/dst coefficient handlers, the blend operation and the clamp — the text
    // route's AppendCoeffBlend spelled out with IR expressions. The separable modes keep the text
    // route's xpBlendColors-missing fallback: the source chain value passes through unchanged
    // (the coverage mix below still applies).
    BlendFormula formula = {};
    ValueID blended = INVALID_VALUE_ID;
    if (BlendModeAsCoeff(blendMode, true, &formula)) {
      const auto one = ir->CreateConstant(1.0f);
      const auto zero = ir->CreateConstant(0.0f);
      const auto oneVec = ir->CreateConstruct(SLType::Float4, {one, one, one, one});
      const auto zeroVec = ir->CreateConstruct(SLType::Float4, {zero, zero, zero, zero});
      ValueID primary = srcValue;
      switch (formula.primaryOutputType()) {
        case BlendFormula::OutputType::None:
          primary = zeroVec;
          break;
        case BlendFormula::OutputType::Coverage:
          primary = covValue;
          break;
        case BlendFormula::OutputType::Modulate:
          primary = ir->CreateBinary(IRBinaryOp::Mul, SLType::Float4, srcValue, covValue);
          break;
        case BlendFormula::OutputType::SAModulate:
          primary = ir->CreateBinary(IRBinaryOp::Mul, SLType::Float4,
                                     ir->CreateSwizzle(srcValue, SLType::Float4, "aaaa"), covValue);
          break;
        case BlendFormula::OutputType::ISAModulate:
          primary = ir->CreateBinary(
              IRBinaryOp::Mul, SLType::Float4,
              ir->CreateBinary(IRBinaryOp::Sub, SLType::Float4, oneVec,
                               ir->CreateSwizzle(srcValue, SLType::Float4, "aaaa")),
              covValue);
          break;
        case BlendFormula::OutputType::ISCModulate:
          primary = ir->CreateBinary(
              IRBinaryOp::Mul, SLType::Float4,
              ir->CreateBinary(IRBinaryOp::Sub, SLType::Float4, oneVec, srcValue), covValue);
          break;
      }
      // The dual-source coefficient modes read a second stage output; the blend formula names its
      // transform with the same output-type set as the primary one.
      ValueID secondary = zeroVec;
      switch (formula.secondaryOutputType()) {
        case BlendFormula::OutputType::None:
          break;
        case BlendFormula::OutputType::Coverage:
          secondary = covValue;
          break;
        case BlendFormula::OutputType::Modulate:
          secondary = ir->CreateBinary(IRBinaryOp::Mul, SLType::Float4, srcValue, covValue);
          break;
        case BlendFormula::OutputType::SAModulate:
          secondary = ir->CreateBinary(IRBinaryOp::Mul, SLType::Float4,
                                       ir->CreateSwizzle(srcValue, SLType::Float4, "aaaa"), covValue);
          break;
        case BlendFormula::OutputType::ISAModulate:
          secondary = ir->CreateBinary(
              IRBinaryOp::Mul, SLType::Float4,
              ir->CreateBinary(IRBinaryOp::Sub, SLType::Float4, oneVec,
                               ir->CreateSwizzle(srcValue, SLType::Float4, "aaaa")),
              covValue);
          break;
        case BlendFormula::OutputType::ISCModulate:
          secondary = ir->CreateBinary(
              IRBinaryOp::Mul, SLType::Float4,
              ir->CreateBinary(IRBinaryOp::Sub, SLType::Float4, oneVec, srcValue), covValue);
          break;
      }
      const auto applyFactor = [&](ValueID base, BlendFactor factor) -> ValueID {
        switch (factor) {
          case BlendFactor::Zero:
            return zeroVec;
          case BlendFactor::One:
            return base;
          case BlendFactor::Src:
            return ir->CreateBinary(IRBinaryOp::Mul, SLType::Float4, base, primary);
          case BlendFactor::OneMinusSrc:
            return ir->CreateBinary(
                IRBinaryOp::Mul, SLType::Float4, base,
                ir->CreateBinary(IRBinaryOp::Sub, SLType::Float4, oneVec, primary));
          case BlendFactor::Dst:
            return ir->CreateBinary(IRBinaryOp::Mul, SLType::Float4, base, dstValue);
          case BlendFactor::OneMinusDst:
            return ir->CreateBinary(
                IRBinaryOp::Mul, SLType::Float4, base,
                ir->CreateBinary(IRBinaryOp::Sub, SLType::Float4, oneVec, dstValue));
          case BlendFactor::SrcAlpha:
            return ir->CreateBinary(IRBinaryOp::Mul, SLType::Float4, base,
                                    ir->CreateSwizzle(primary, SLType::Float4, "aaaa"));
          case BlendFactor::OneMinusSrcAlpha:
            return ir->CreateBinary(
                IRBinaryOp::Mul, SLType::Float4, base,
                ir->CreateBinary(IRBinaryOp::Sub, SLType::Float4, oneVec,
                                 ir->CreateSwizzle(primary, SLType::Float4, "aaaa")));
          case BlendFactor::DstAlpha:
            return ir->CreateBinary(IRBinaryOp::Mul, SLType::Float4, base,
                                    ir->CreateSwizzle(dstValue, SLType::Float4, "aaaa"));
          case BlendFactor::OneMinusDstAlpha:
            return ir->CreateBinary(
                IRBinaryOp::Mul, SLType::Float4, base,
                ir->CreateBinary(IRBinaryOp::Sub, SLType::Float4, oneVec,
                                 ir->CreateSwizzle(dstValue, SLType::Float4, "aaaa")));
          case BlendFactor::Src1:
            return ir->CreateBinary(IRBinaryOp::Mul, SLType::Float4, base, secondary);
          case BlendFactor::OneMinusSrc1:
            return ir->CreateBinary(
                IRBinaryOp::Mul, SLType::Float4, base,
                ir->CreateBinary(IRBinaryOp::Sub, SLType::Float4, oneVec, secondary));
          case BlendFactor::Src1Alpha:
            return ir->CreateBinary(IRBinaryOp::Mul, SLType::Float4, base,
                                    ir->CreateSwizzle(secondary, SLType::Float4, "aaaa"));
          case BlendFactor::OneMinusSrc1Alpha:
            return ir->CreateBinary(
                IRBinaryOp::Mul, SLType::Float4, base,
                ir->CreateBinary(IRBinaryOp::Sub, SLType::Float4, oneVec,
                                 ir->CreateSwizzle(secondary, SLType::Float4, "aaaa")));
        }
        return INVALID_VALUE_ID;
      };
      const auto applyOp = [&](ValueID srcTerm, ValueID dstTerm,
                               BlendOperation operation) -> ValueID {
        switch (operation) {
          case BlendOperation::Min:
            return ir->CreateBinary(IRBinaryOp::Min, SLType::Float4, srcTerm, dstTerm);
          case BlendOperation::Max:
            return ir->CreateBinary(IRBinaryOp::Max, SLType::Float4, srcTerm, dstTerm);
          case BlendOperation::Add:
          case BlendOperation::Subtract:
          case BlendOperation::ReverseSubtract: {
            const auto sum = operation == BlendOperation::Add
                                 ? ir->CreateBinary(IRBinaryOp::Add, SLType::Float4, srcTerm,
                                                    dstTerm)
                             : operation == BlendOperation::Subtract
                                 ? ir->CreateBinary(IRBinaryOp::Sub, SLType::Float4, srcTerm,
                                                    dstTerm)
                                 : ir->CreateBinary(IRBinaryOp::Sub, SLType::Float4, dstTerm,
                                                    srcTerm);
            return ir->CreateBinary(
                IRBinaryOp::Min, SLType::Float4,
                ir->CreateBinary(IRBinaryOp::Max, SLType::Float4, sum, zeroVec), oneVec);
          }
        }
        return INVALID_VALUE_ID;
      };
      const auto srcWithCoeff = applyFactor(primary, formula.srcFactor());
      const auto dstWithCoeff = applyFactor(dstValue, formula.dstFactor());
      if (srcWithCoeff == INVALID_VALUE_ID || dstWithCoeff == INVALID_VALUE_ID) {
        return;
      }
      blended = applyOp(srcWithCoeff, dstWithCoeff, formula.operation());
    } else {
      // The separable modes call their XpBlend operator: the text route's inline generators
      // (Darken's min, HardLight's component branches, ...) now live in fn/xp/xpBlend_*.inc, one
      // function per mode, and the coverage mix below finishes the tail.
      const auto sepSrcAlpha = ir->CreateSwizzle(srcValue, SLType::Float4, "aaaa");
      const auto sepDstAlpha = ir->CreateSwizzle(dstValue, SLType::Float4, "aaaa");
      const auto sepDstSrcAlpha =
          ir->CreateBinary(IRBinaryOp::Mul, SLType::Float4, dstValue, sepSrcAlpha);
      const auto sepSrcDstAlpha =
          ir->CreateBinary(IRBinaryOp::Mul, SLType::Float4, srcValue, sepDstAlpha);
#define EMIT_SEP_CASE(name)                                                                        \
  case BlendMode::name:                                                                            \
    blended = XpBlend##name##Op::Emit(ir, uniformHandler, nullptr, srcValue, dstValue,             \
                                      sepDstSrcAlpha, sepSrcDstAlpha);                             \
    break;
      switch (blendMode) {
        EMIT_SEP_CASE(Overlay)
        EMIT_SEP_CASE(Darken)
        EMIT_SEP_CASE(Lighten)
        EMIT_SEP_CASE(ColorDodge)
        EMIT_SEP_CASE(ColorBurn)
        EMIT_SEP_CASE(HardLight)
        EMIT_SEP_CASE(SoftLight)
        EMIT_SEP_CASE(Difference)
        EMIT_SEP_CASE(Exclusion)
        EMIT_SEP_CASE(Multiply)
        EMIT_SEP_CASE(Hue)
        EMIT_SEP_CASE(Saturation)
        EMIT_SEP_CASE(Color)
        EMIT_SEP_CASE(Luminosity)
        EMIT_SEP_CASE(PlusDarker)
        default:
          break;
      }
#undef EMIT_SEP_CASE
    }
    if (blended == INVALID_VALUE_ID) {
      ir->MarkFailed("the porter-duff blend mode has no XpBlend operator");
      return;
    }
    ValueID result = blended;
    if (!BlendModeAsCoeff(blendMode, true)) {
      const auto covered = ir->CreateBinary(IRBinaryOp::Mul, SLType::Float4, blended,
                                            args.inputCoverageValue);
      const auto one = ir->CreateConstant(1.0f);
      const auto white = ir->CreateConstruct(SLType::Float4, {one, one, one, one});
      const auto oneMinusCoverage =
          ir->CreateBinary(IRBinaryOp::Sub, SLType::Float4, white, args.inputCoverageValue);
      const auto uncovered =
          ir->CreateBinary(IRBinaryOp::Mul, SLType::Float4, oneMinusCoverage, dstValue);
      result = ir->CreateBinary(IRBinaryOp::Add, SLType::Float4, covered, uncovered);
    }
    ir->StoreOutput(args.outputValue, result);
    return;
  }

  if (args.dstTextureSamplerHandle.isValid()) {
    // While shaders typically don't output negative coverage, we use <= as a precaution against
    // floating point precision errors. We only check the rgb values since the alpha might not be
    // set when using lcd. If we're using single channel coverage, alpha will match rgb anyway.
    //
    // Discarding here also helps batch text draws that need to read from a dst copy for blends.
    // This is particularly useful when the outer bounding boxes of each letter overlap, though it
    // doesn't help when actual parts of the text overlap.
    fragBuilder->codeAppendf("if (%s.r <= 0.0 && %s.g <= 0.0 && %s.b <= 0.0) {",
                             args.inputCoverage.c_str(), args.inputCoverage.c_str(),
                             args.inputCoverage.c_str());
    fragBuilder->codeAppend("discard;");
    fragBuilder->codeAppend("}");

    auto dstTopLeftName = uniformHandler->addUniform("DstTextureUpperLeft", UniformFormat::Float2,
                                                     ShaderStage::Fragment);
    auto dstCoordScaleName = uniformHandler->addUniform(
        "DstTextureCoordScale", UniformFormat::Float2, ShaderStage::Fragment);

    // One call owns the whole tail: the zero-coverage discard, the destination read and the
    // coverage blend (the assembled file spells exactly what the text below spells). The
    // dual-source coefficient modes keep the text: their second output has no single-value form.
    auto porterDuffEntry = fragBuilder->appendFunction("applyPorterDuffXP");
    BlendFormula blendFormula = {};
    const bool dualSource =
        BlendModeAsCoeff(blendMode, true, &blendFormula) && blendFormula.needSecondaryOutput();
    if (!porterDuffEntry.empty() && !dualSource) {
      const auto dstSamplerName =
          uniformHandler->getSamplerVariable(args.dstTextureSamplerHandle).name();
      fragBuilder->appendCall(
          args.outputColor, porterDuffEntry,
          {args.inputColor, args.inputCoverage, "gl_FragCoord.xy", dstTopLeftName,
           dstCoordScaleName, std::to_string(static_cast<int>(blendMode)), dstSamplerName});
      // The call folded the coverage itself (the zero-coverage discard and the coverage blend
      // live inside it), so it is the stage's root and the epilogue must not combine again.
      fragBuilder->recordStageRoot(args.outputColor);
      return;
    }

    fragBuilder->codeAppend("// Read color from copy of the destination.\n");
    std::string dstTexCoord = "_dstTexCoord";
    fragBuilder->codeAppendf("highp vec2 %s = (gl_FragCoord.xy - %s) * %s;", dstTexCoord.c_str(),
                             dstTopLeftName.c_str(), dstCoordScaleName.c_str());

    fragBuilder->codeAppendf("vec4 %s = ", dstColor.c_str());
    fragBuilder->appendTextureLookup(args.dstTextureSamplerHandle, dstTexCoord);
    fragBuilder->codeAppend(";");
  }

  const char* outColor = "localOutputColor";
  fragBuilder->codeAppendf("vec4 %s;", outColor);
  AppendMode(fragBuilder, args.inputColor, args.inputCoverage, dstColor, outColor, blendMode, true);

  if (!BlendModeAsCoeff(blendMode, true)) {
    fragBuilder->codeAppendf("%s = %s * %s + (vec4(1.0) - %s) * %s;", outColor,
                             args.inputCoverage.c_str(), outColor, args.inputCoverage.c_str(),
                             dstColor.c_str());
  }
  fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), outColor);
}

void GLSLPorterDuffXferProcessor::setData(UniformData* /*vertexUniformData*/,
                                          UniformData* fragmentUniformData) const {
  if (dstTextureInfo.textureProxy == nullptr) {
    return;
  }
  auto dstTextureView = dstTextureInfo.textureProxy->getTextureView();
  if (dstTextureView == nullptr) {
    return;
  }
  fragmentUniformData->setData("DstTextureUpperLeft", dstTextureInfo.offset);
  int width;
  int height;
  if (dstTextureView->getTexture()->type() == TextureType::Rectangle) {
    width = 1;
    height = 1;
  } else {
    width = dstTextureView->width();
    height = dstTextureView->height();
  }
  float scales[] = {1.f / static_cast<float>(width), 1.f / static_cast<float>(height)};
  fragmentUniformData->setData("DstTextureCoordScale", scales);
}
}  // namespace tgfx
