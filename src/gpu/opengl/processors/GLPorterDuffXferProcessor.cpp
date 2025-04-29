/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "GLPorterDuffXferProcessor.h"

namespace tgfx {

static void AppendOutputColor(FragmentShaderBuilder* fragBuilder,
                              BlendFormula::OutputType outputType, const std::string& output,
                              const std::string& inColor, const std::string& inCoverage) {
  switch (outputType) {
    case BlendFormula::OutputType::None:
      fragBuilder->codeAppendf("%s = vec4(0.0);", output.c_str());
      break;
    case BlendFormula::Coverage:
      fragBuilder->codeAppendf("%s = %s;", output.c_str(), inCoverage.c_str());
      break;
    case BlendFormula::Modulate:
      fragBuilder->codeAppendf("%s = %s * %s;", output.c_str(), inColor.c_str(),
                               inCoverage.c_str());
      break;
    case BlendFormula::SAModulate:
      fragBuilder->codeAppendf("%s = %s.a * %s;", output.c_str(), inColor.c_str(),
                               inCoverage.c_str());
      break;
    case BlendFormula::ISAModulate:
      fragBuilder->codeAppendf("%s = (1.0 - %s.a) * %s;", output.c_str(), inColor.c_str(),
                               inCoverage.c_str());
      break;
    case BlendFormula::ISCModulate:
      fragBuilder->codeAppendf("%s = (vec4(1.0) - %s) * %s;", output.c_str(), inColor.c_str(),
                               inCoverage.c_str());
      break;
    default:
      break;
  }
}

static void AppendCoeff(FragmentShaderBuilder* fragBuilder, BlendModeCoeff coeff,
                        const std::string& input1, const std::string& input2,
                        const std::string& dstColor, const std::string& output) {
  switch (coeff) {
    case BlendModeCoeff::Zero:
      fragBuilder->codeAppendf("%s = vec4(0.0);", output.c_str());
      break;
    case BlendModeCoeff::One:
      fragBuilder->codeAppendf("%s = vec4(1.0);", output.c_str());
      break;
    case BlendModeCoeff::SC:
      fragBuilder->codeAppendf("%s = %s;", output.c_str(), input1.c_str());
      break;
    case BlendModeCoeff::ISC:
      fragBuilder->codeAppendf("%s = (1.0 - %s);", output.c_str(), input1.c_str(), input2.c_str());
      break;
    case BlendModeCoeff::DC:
      fragBuilder->codeAppendf("%s = %s;", output.c_str(), dstColor.c_str());
      break;
    case BlendModeCoeff::IDC:
      fragBuilder->codeAppendf("%s = (1.0 - %s);", output.c_str(), dstColor.c_str());
      break;
    case BlendModeCoeff::SA:
      fragBuilder->codeAppendf("%s = vec4(%s.a);", output.c_str(), input1.c_str());
      break;
    case BlendModeCoeff::ISA:
      fragBuilder->codeAppendf("%s = vec4(1.0 - %s.a);", output.c_str(), input1.c_str());
      break;
    case BlendModeCoeff::DA:
      fragBuilder->codeAppendf("%s = vec4(%s.a);", output.c_str(), dstColor.c_str());
      break;
    case BlendModeCoeff::IDA:
      fragBuilder->codeAppendf("%s = vec4(1.0 - %s.a);", output.c_str(), dstColor.c_str());
      break;
    case BlendModeCoeff::S2A:
      fragBuilder->codeAppendf("%s = vec4(%s.a);", output.c_str(), input2.c_str());
      break;
    case BlendModeCoeff::IS2A:
      fragBuilder->codeAppendf("%s = vec4(1.0 - %s.a);", output.c_str(), input2.c_str());
      break;
    case BlendModeCoeff::S2C:
      fragBuilder->codeAppendf("%s = %s;", output.c_str(), input2.c_str());
      break;
    case BlendModeCoeff::IS2C:
      fragBuilder->codeAppendf("%s = (1.0 - %s);", output.c_str(), input2.c_str());
      break;
  }
}

static void AppendBlend(FragmentShaderBuilder* fragBuilder, BlendEquation equation,
                        BlendModeCoeff srcCoeff, BlendModeCoeff dstCoeff, const std::string& input1,
                        const std::string& input2, const std::string& dstColor,
                        const std::string& output) {
  auto dstCoeffName = "dstCoeff";
  fragBuilder->codeAppendf("vec4 %s = vec4(1.0f);", dstCoeffName);
  AppendCoeff(fragBuilder, dstCoeff, input1, input2, dstColor, dstCoeffName);
  auto srcCoeffName = "srcCoeff";
  fragBuilder->codeAppendf("vec4 %s = vec4(1.0f);", srcCoeffName);
  AppendCoeff(fragBuilder, srcCoeff, input1, input2, dstColor, srcCoeffName);
  switch (equation) {
    case BlendEquation::Add:
      fragBuilder->codeAppendf("%s = %s * %s + %s * %s;", output.c_str(), input1.c_str(),
                               srcCoeffName, dstColor.c_str(), dstCoeffName);
      break;
    case BlendEquation::Subtract:
      fragBuilder->codeAppendf("%s = %s * %s - %s * %s;", output.c_str(), input1.c_str(),
                               srcCoeffName, dstColor.c_str(), dstCoeffName);
      break;
    case BlendEquation::ReverseSubtract:
      fragBuilder->codeAppendf("%s = %s * %s - %s * %s;", output.c_str(), dstColor.c_str(),
                               dstCoeffName, input1.c_str(), srcCoeffName);
      break;
  }
}

PlacementPtr<PorterDuffXferProcessor> PorterDuffXferProcessor::Make(
    BlockBuffer* buffer, const BlendFormula& formula, const DstTextureInfo& dstTextureInfo) {
  return buffer->make<GLPorterDuffXferProcessor>(formula, dstTextureInfo);
}

GLPorterDuffXferProcessor::GLPorterDuffXferProcessor(const BlendFormula& blendFormula,
                                                     const DstTextureInfo& dstTextureInfo)
    : PorterDuffXferProcessor(blendFormula, dstTextureInfo) {
}

void GLPorterDuffXferProcessor::emitCode(const EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  auto uniformHandler = args.uniformHandler;

  std::string primaryOutputColor = "primaryOutputColor";

  args.fragBuilder->codeAppendf("vec4 %s = vec4(0.0);", primaryOutputColor.c_str());
  AppendOutputColor(args.fragBuilder, blendFormula.primaryOutputType, primaryOutputColor,
                    args.inputColor, args.inputCoverage);
  if (!blendFormula.needSecondaryOutput()) {
    fragBuilder->codeAppendf("%s = %s", args.outputColor.c_str(), primaryOutputColor.c_str());
    return;
  }

  const auto& dstColor = fragBuilder->dstColor();
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

    auto dstTopLeftName =
        uniformHandler->addUniform(ShaderFlags::Fragment, SLType::Float2, "DstTextureUpperLeft");
    auto dstCoordScaleName =
        uniformHandler->addUniform(ShaderFlags::Fragment, SLType::Float2, "DstTextureCoordScale");

    fragBuilder->codeAppend("// Read color from copy of the destination.\n");
    std::string dstTexCoord = "_dstTexCoord";
    fragBuilder->codeAppendf("vec2 %s = (gl_FragCoord.xy - %s) * %s;", dstTexCoord.c_str(),
                             dstTopLeftName.c_str(), dstCoordScaleName.c_str());

    fragBuilder->codeAppendf("vec4 %s = ", dstColor.c_str());
    fragBuilder->appendTextureLookup(args.dstTextureSamplerHandle, dstTexCoord);
    fragBuilder->codeAppend(";");
  }

  std::string secondaryOutputColor = "secondaryOutputColor";
  args.fragBuilder->codeAppendf("vec4 %s = vec4(0.0);", secondaryOutputColor.c_str());
  AppendOutputColor(args.fragBuilder, blendFormula.secondaryOutputType, secondaryOutputColor,
                    args.inputColor, args.inputCoverage);

  AppendBlend(args.fragBuilder, blendFormula.equation, blendFormula.srcBlend, blendFormula.dstBlend,
              primaryOutputColor, secondaryOutputColor, dstColor, args.outputColor);
}

void GLPorterDuffXferProcessor::setData(UniformBuffer*) const {
}

}  // namespace tgfx
