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

#include "GLSLBlend.h"
#include "gpu/BlendFormula.h"

namespace tgfx {

static void HandleBlendModes(FragmentShaderBuilder* fsBuilder, const std::string& srcColor,
                             const std::string& dstColor, const std::string& outputColor,
                             BlendMode blendMode) {
  // One dispatcher call: the mode travels as a literal, the two premultiplied operands the
  // hue-family modes read are computed inside the function, and the whole tail is one recorded
  // call the bytecode routes can link.
  auto entry = fsBuilder->appendFunction("xpBlendColors");
  if (entry.empty()) {
    // A build without the catalog cannot serve the advanced modes: pass the source color through
    // so the draw still completes instead of reading uninitialized output.
    fsBuilder->codeAppendf("%s = %s;", outputColor.c_str(), srcColor.c_str());
    return;
  }
  fsBuilder->appendCall(outputColor, entry,
                        {srcColor, dstColor, std::to_string(static_cast<int>(blendMode))});
}

static void OutputHandler_None(FragmentShaderBuilder* fragBuilder, const char*, const char*) {
  fragBuilder->codeAppendf("vec4(0.0);");
}

static void OutputHandler_Coverage(FragmentShaderBuilder* fragBuilder, const char*,
                                   const char* coverage) {
  fragBuilder->codeAppendf("%s;", coverage);
}

static void OutputHandler_Modulate(FragmentShaderBuilder* fragBuilder, const char* srcColor,
                                   const char* coverage) {
  fragBuilder->codeAppendf("%s * %s;", srcColor, coverage);
}

static void OutputHandler_SAModulate(FragmentShaderBuilder* fragBuilder, const char* srcColor,
                                     const char* coverage) {
  fragBuilder->codeAppendf("%s.a * %s;", srcColor, coverage);
}

static void OutputHandler_ISAModulate(FragmentShaderBuilder* fragBuilder, const char* srcColor,
                                      const char* coverage) {
  fragBuilder->codeAppendf("(1.0 - %s.a) * %s;", srcColor, coverage);
}

static void OutputHandler_ISCModulate(FragmentShaderBuilder* fragBuilder, const char* srcColor,
                                      const char* coverage) {
  fragBuilder->codeAppendf("(vec4(1.0) - %s) * %s;", srcColor, coverage);
}

using OutputHandler = void (*)(FragmentShaderBuilder* fragBuilder, const char* srcColor,
                               const char* coverage);

static constexpr OutputHandler OUTPUT_HANDLERS[] = {
    OutputHandler_None,       OutputHandler_Coverage,    OutputHandler_Modulate,
    OutputHandler_SAModulate, OutputHandler_ISAModulate, OutputHandler_ISCModulate};

static void CoeffHandler_ONE(FragmentShaderBuilder* fsBuilder, const char*, const char*,
                             const char*) {
  fsBuilder->codeAppend(";");
}

static void CoeffHandler_SRC_COLOR(FragmentShaderBuilder* fsBuilder, const char* srcColorName,
                                   const char*, const char*) {
  fsBuilder->codeAppendf(" * %s;", srcColorName);
}

static void CoeffHandler_ONE_MINUS_SRC_COLOR(FragmentShaderBuilder* fsBuilder,
                                             const char* srcColorName, const char*, const char*) {
  fsBuilder->codeAppendf(" * (vec4(1.0) - %s);", srcColorName);
}

static void CoeffHandler_DST_COLOR(FragmentShaderBuilder* fsBuilder, const char*, const char*,
                                   const char* dstColorName) {
  fsBuilder->codeAppendf(" * %s;", dstColorName);
}

static void CoeffHandler_ONE_MINUS_DST_COLOR(FragmentShaderBuilder* fsBuilder, const char*,
                                             const char*, const char* dstColorName) {
  fsBuilder->codeAppendf(" * (vec4(1.0) - %s);", dstColorName);
}

static void CoeffHandler_SRC_ALPHA(FragmentShaderBuilder* fsBuilder, const char* srcColorName,
                                   const char*, const char*) {
  fsBuilder->codeAppendf(" * %s.a;", srcColorName);
}

static void CoeffHandler_ONE_MINUS_SRC_ALPHA(FragmentShaderBuilder* fsBuilder,
                                             const char* srcColorName, const char*, const char*) {
  fsBuilder->codeAppendf(" * (1.0 - %s.a);", srcColorName);
}

static void CoeffHandler_DST_ALPHA(FragmentShaderBuilder* fsBuilder, const char*, const char*,
                                   const char* dstColorName) {
  fsBuilder->codeAppendf(" * %s.a;", dstColorName);
}

static void CoeffHandler_ONE_MINUS_DST_ALPHA(FragmentShaderBuilder* fsBuilder, const char*,
                                             const char*, const char* dstColorName) {
  fsBuilder->codeAppendf(" * (1.0 - %s.a);", dstColorName);
}

static void CoeffHandler_SRC1_COLOR(FragmentShaderBuilder* fsBuilder, const char*,
                                    const char* src1ColorName, const char*) {
  fsBuilder->codeAppendf(" * %s;", src1ColorName);
}
static void CoeffHandler_ONE_MINUS_SRC1_COLOR(FragmentShaderBuilder* fsBuilder, const char*,
                                              const char* src1ColorName, const char*) {
  fsBuilder->codeAppendf(" * (vec4(1.0) - %s);", src1ColorName);
}

static void CoeffHandler_SRC1_ALPHA(FragmentShaderBuilder* fsBuilder, const char*,
                                    const char* src1ColorName, const char*) {
  fsBuilder->codeAppendf(" * %s.a;", src1ColorName);
}

static void CoeffHandler_ONE_MINUS_SRC1_ALPHA(FragmentShaderBuilder* fsBuilder, const char*,
                                              const char* src1ColorName, const char*) {
  fsBuilder->codeAppendf(" * (1.0 - %s.a);", src1ColorName);
}

using CoeffHandler = void (*)(FragmentShaderBuilder* fsBuilder, const char* srcColorName,
                              const char* src1ColorName, const char* dstColorName);

static constexpr CoeffHandler COEFF_HANDLERS[] = {CoeffHandler_ONE,
                                                  CoeffHandler_SRC_COLOR,
                                                  CoeffHandler_ONE_MINUS_SRC_COLOR,
                                                  CoeffHandler_DST_COLOR,
                                                  CoeffHandler_ONE_MINUS_DST_COLOR,
                                                  CoeffHandler_SRC_ALPHA,
                                                  CoeffHandler_ONE_MINUS_SRC_ALPHA,
                                                  CoeffHandler_DST_ALPHA,
                                                  CoeffHandler_ONE_MINUS_DST_ALPHA,
                                                  CoeffHandler_SRC1_COLOR,
                                                  CoeffHandler_ONE_MINUS_SRC1_COLOR,
                                                  CoeffHandler_SRC1_ALPHA,
                                                  CoeffHandler_ONE_MINUS_SRC1_ALPHA};

void AppendCoeffBlend(FragmentShaderBuilder* fsBuilder, const std::string& srcColor,
                      const std::string& coverageColor, const std::string& dstColor,
                      const std::string& outColor, const BlendFormula& formula) {
  std::string primaryOutputColor = "primaryOutputColor";
  fsBuilder->codeAppendf("vec4 %s = ", primaryOutputColor.c_str());
  OUTPUT_HANDLERS[static_cast<int>(formula.primaryOutputType())](fsBuilder, srcColor.c_str(),
                                                                 coverageColor.c_str());

  std::string secondaryOutputColor;
  if (formula.needSecondaryOutput()) {
    secondaryOutputColor = "secondaryOutputColor";
    fsBuilder->codeAppendf("vec4 %s = ", secondaryOutputColor.c_str());
    OUTPUT_HANDLERS[static_cast<int>(formula.secondaryOutputType())](fsBuilder, srcColor.c_str(),
                                                                     coverageColor.c_str());
  }

  auto dstWithCoeff = "dst";
  if (formula.dstFactor() == BlendFactor::Zero) {
    fsBuilder->codeAppendf("vec4 %s = vec4(0.0);", dstWithCoeff);
  } else {
    fsBuilder->codeAppendf("vec4 %s = %s", dstWithCoeff, dstColor.c_str());
    COEFF_HANDLERS[static_cast<int>(formula.dstFactor()) - 1](
        fsBuilder, primaryOutputColor.c_str(), secondaryOutputColor.c_str(), dstColor.c_str());
  }

  auto srcWithCoeff = "src";
  if (formula.srcFactor() == BlendFactor::Zero) {
    fsBuilder->codeAppendf("vec4 %s = vec4(0.0);", srcWithCoeff);
  } else {
    fsBuilder->codeAppendf("vec4 %s = %s", srcWithCoeff, primaryOutputColor.c_str());
    COEFF_HANDLERS[static_cast<int>(formula.srcFactor()) - 1](
        fsBuilder, primaryOutputColor.c_str(), secondaryOutputColor.c_str(), dstColor.c_str());
  }

  switch (formula.operation()) {
    case BlendOperation::Add:
      fsBuilder->codeAppendf("%s = clamp(%s + %s, 0.0, 1.0);", outColor.c_str(), srcWithCoeff,
                             dstWithCoeff);
      break;
    case BlendOperation::Subtract:
      fsBuilder->codeAppendf("%s = clamp(%s - %s , 0.0, 1.0);", outColor.c_str(), srcWithCoeff,
                             dstWithCoeff);
      break;
    case BlendOperation::ReverseSubtract:
      fsBuilder->codeAppendf("%s = clamp(%s - %s, 0.0, 1.0);", outColor.c_str(), dstWithCoeff,
                             srcWithCoeff);
      break;
    case BlendOperation::Min:
      fsBuilder->codeAppendf("%s = min(%s, %s);", outColor.c_str(), srcWithCoeff, dstWithCoeff);
      break;
    case BlendOperation::Max:
      fsBuilder->codeAppendf("%s = max(%s, %s);", outColor.c_str(), srcWithCoeff, dstWithCoeff);
      break;
  }
}

void AppendMode(FragmentShaderBuilder* fsBuilder, const std::string& srcColor,
                const std::string& coverageColor, const std::string& dstColor,
                const std::string& outColor, BlendMode blendMode, bool hasCoverageProcessor) {
  BlendFormula blendInfo = {};
  if (BlendModeAsCoeff(blendMode, hasCoverageProcessor, &blendInfo)) {
    AppendCoeffBlend(fsBuilder, srcColor, coverageColor, dstColor, outColor, blendInfo);
  } else {
    HandleBlendModes(fsBuilder, srcColor, dstColor, outColor, blendMode);
  }
}

const char* BlendModeName(BlendMode mode) {
  const char* ModeStrings[] = {
      "Clear",       "Src",       "Dst",        "SrcOver",   "DstOver",    "SrcIn",
      "DstIn",       "SrcOut",    "DstOut",     "SrcATop",   "DstATop",    "Xor",
      "PlusLighter", "Modulate",  "Screen",     "Overlay",   "Darken",     "Lighten",
      "ColorDodge",  "ColorBurn", "HardLight",  "SoftLight", "Difference", "Exclusion",
      "Multiply",    "Hue",       "Saturation", "Color",     "Luminosity", "PlusDarker",
  };
  return ModeStrings[static_cast<int>(mode)];
}
}  // namespace tgfx
