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

#include "GLSLUnrolledBinaryGradientColorizer.h"
#include "core/utils/MathExtra.h"

namespace tgfx {
struct UnrolledBinaryUniformName {
  std::string scale0_1;
  std::string scale2_3;
  std::string scale4_5;
  std::string scale6_7;
  std::string scale8_9;
  std::string scale10_11;
  std::string scale12_13;
  std::string scale14_15;
  std::string bias0_1;
  std::string bias2_3;
  std::string bias4_5;
  std::string bias6_7;
  std::string bias8_9;
  std::string bias10_11;
  std::string bias12_13;
  std::string bias14_15;
  std::string thresholds1_7;
  std::string thresholds9_13;
};

static constexpr int MaxIntervals = 8;

PlacementPtr<UnrolledBinaryGradientColorizer> UnrolledBinaryGradientColorizer::Make(
    BlockBuffer* buffer, const Color* colors, const float* positions, int count) {
  // Depending on how the positions resolve into hard stops or regular stops, the number of
  // intervals specified by the number of colors/positions can change. For instance, a plain
  // 3 color gradient is two intervals, but a 4 color gradient with a hard stop is also
  // two intervals. At the most extreme end, an 8 interval gradient made entirely of hard
  // stops has 16 colors.

  if (count > MaxColorCount) {
    // Definitely cannot represent this gradient configuration
    return nullptr;
  }

  // The raster implementation also uses scales and biases, but since they must be calculated
  // after the dst color space is applied, it limits our ability to cache their values.
  Color scales[MaxIntervals];
  Color biases[MaxIntervals];
  float thresholds[MaxIntervals];

  int intervalCount = 0;

  for (int i = 0; i < count - 1; i++) {
    if (intervalCount >= MaxIntervals) {
      // Already reached MaxIntervals, and haven't run out of color stops so this
      // gradient cannot be represented by this shader.
      return nullptr;
    }

    auto t0 = positions[i];
    auto t1 = positions[i + 1];
    auto dt = t1 - t0;
    // If the interval is empty, skip to the next interval. This will automatically create
    // distinct hard stop intervals as needed. It also protects against malformed gradients
    // that have repeated hard stops at the very beginning that are effectively unreachable.
    if (FloatNearlyZero(dt)) {
      continue;
    }

    for (int j = 0; j < 4; ++j) {
      auto c0 = colors[i][j];
      auto c1 = colors[i + 1][j];
      auto scale = (c1 - c0) / dt;
      auto bias = c0 - t0 * scale;
      scales[intervalCount][j] = scale;
      biases[intervalCount][j] = bias;
    }
    thresholds[intervalCount] = t1;
    intervalCount++;
  }

  // set the unused values to something consistent
  for (int i = intervalCount; i < MaxIntervals; i++) {
    scales[i] = Color::Transparent();
    biases[i] = Color::Transparent();
    thresholds[i] = 0.0;
  }

  return buffer->make<GLSLUnrolledBinaryGradientColorizer>(
      intervalCount, scales, biases,
      Rect::MakeLTRB(thresholds[0], thresholds[1], thresholds[2], thresholds[3]),
      Rect::MakeLTRB(thresholds[4], thresholds[5], thresholds[6], 0.0));
}

GLSLUnrolledBinaryGradientColorizer::GLSLUnrolledBinaryGradientColorizer(
    int intervalCount, Color* scales, Color* biases, Rect thresholds1_7, Rect thresholds9_13)
    : UnrolledBinaryGradientColorizer(intervalCount, scales, biases, thresholds1_7,
                                      thresholds9_13) {
}

std::string AddUniform(UniformHandler* uniformHandler, const std::string& name, int intervalCount,
                       int limit) {
  if (intervalCount > limit) {
    return uniformHandler->addUniform(name, UniformFormat::Float4, ShaderStage::Fragment);
  }
  return "";
}

void AppendCode1(FragmentShaderBuilder* fragBuilder, int intervalCount,
                 const UnrolledBinaryUniformName& name) {
  if (intervalCount >= 2) {
    fragBuilder->codeAppend("// thresholds1_7.y is mid-point for intervals (0,3) and (4,7)\n");
    fragBuilder->codeAppendf("if (t < %s.y) {", name.thresholds1_7.c_str());
  }
  fragBuilder->codeAppend("// thresholds1_7.x is mid-point for intervals (0,1) and (2,3)\n");
  fragBuilder->codeAppendf("if (t < %s.x) {", name.thresholds1_7.c_str());
  fragBuilder->codeAppendf("scale = %s;", name.scale0_1.c_str());
  fragBuilder->codeAppendf("bias = %s;", name.bias0_1.c_str());
  if (intervalCount > 1) {
    fragBuilder->codeAppend("} else {");
    fragBuilder->codeAppendf("scale = %s;",
                             name.scale2_3.empty() ? "vec4(0.0)" : name.scale2_3.c_str());
    fragBuilder->codeAppendf("bias = %s;",
                             name.bias2_3.empty() ? "vec4(0.0)" : name.bias2_3.c_str());
  }
  fragBuilder->codeAppend("}");
  if (intervalCount > 2) {
    fragBuilder->codeAppend("} else {");
  }
  if (intervalCount >= 3) {
    fragBuilder->codeAppend("// thresholds1_7.z is mid-point for intervals (4,5) and (6,7)\n");
    fragBuilder->codeAppendf("if (t < %s.z) {", name.thresholds1_7.c_str());
    fragBuilder->codeAppendf("scale = %s;",
                             name.scale4_5.empty() ? "vec4(0.0)" : name.scale4_5.c_str());
    fragBuilder->codeAppendf("bias = %s;",
                             name.bias4_5.empty() ? "vec4(0.0)" : name.bias4_5.c_str());
  }
  if (intervalCount > 3) {
    fragBuilder->codeAppend("} else {");
    fragBuilder->codeAppendf("scale = %s;",
                             name.scale6_7.empty() ? "vec4(0.0)" : name.scale6_7.c_str());
    fragBuilder->codeAppendf("bias = %s;",
                             name.bias6_7.empty() ? "vec4(0.0)" : name.bias6_7.c_str());
  }
  if (intervalCount >= 3) {
    fragBuilder->codeAppend("}");
  }
  if (intervalCount >= 2) {
    fragBuilder->codeAppend("}");
  }
}

void AppendCode2(FragmentShaderBuilder* fragBuilder, int intervalCount,
                 const UnrolledBinaryUniformName& name) {
  if (intervalCount >= 6) {
    fragBuilder->codeAppend("// thresholds9_13.y is mid-point for intervals (8,11) and (12,15)\n");
    fragBuilder->codeAppendf("if (t < %s.y) {", name.thresholds9_13.c_str());
  }
  if (intervalCount >= 5) {
    fragBuilder->codeAppend("// thresholds9_13.x is mid-point for intervals (8,9) and (10,11)\n");
    fragBuilder->codeAppendf("if (t < %s.x) {", name.thresholds9_13.c_str());
    fragBuilder->codeAppendf("scale = %s;",
                             name.scale8_9.empty() ? "vec4(0.0)" : name.scale8_9.c_str());
    fragBuilder->codeAppendf("bias = %s;",
                             name.bias8_9.empty() ? "vec4(0.0)" : name.bias8_9.c_str());
  }
  if (intervalCount > 5) {
    fragBuilder->codeAppend("} else {");
    fragBuilder->codeAppendf("scale = %s;",
                             name.scale10_11.empty() ? "vec4(0.0)" : name.scale10_11.c_str());
    fragBuilder->codeAppendf("bias = %s;",
                             name.bias10_11.empty() ? "vec4(0.0)" : name.bias10_11.c_str());
  }

  if (intervalCount >= 5) {
    fragBuilder->codeAppend("}");
  }
  if (intervalCount > 6) {
    fragBuilder->codeAppend("} else {");
  }
  if (intervalCount >= 7) {
    fragBuilder->codeAppend("// thresholds9_13.z is mid-point for intervals (12,13) and (14,15)\n");
    fragBuilder->codeAppendf("if (t < %s.z) {", name.thresholds9_13.c_str());
    fragBuilder->codeAppendf("scale = %s;",
                             name.scale12_13.empty() ? "vec4(0.0)" : name.scale12_13.c_str());
    fragBuilder->codeAppendf("bias = %s;",
                             name.bias12_13.empty() ? "vec4(0.0)" : name.bias12_13.c_str());
  }
  if (intervalCount > 7) {
    fragBuilder->codeAppend("} else {");
    fragBuilder->codeAppendf("scale = %s;",
                             name.scale14_15.empty() ? "vec4(0.0)" : name.scale14_15.c_str());
    fragBuilder->codeAppendf("bias = %s;",
                             name.bias14_15.empty() ? "vec4(0.0)" : name.bias14_15.c_str());
  }
  if (intervalCount >= 7) {
    fragBuilder->codeAppend("}");
  }
  if (intervalCount >= 6) {
    fragBuilder->codeAppend("}");
  }
}

void GLSLUnrolledBinaryGradientColorizer::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  auto uniformHandler = args.uniformHandler;
  UnrolledBinaryUniformName uniformNames = {};
  uniformNames.scale0_1 = AddUniform(uniformHandler, "scale0_1", intervalCount, 0);
  uniformNames.scale2_3 = AddUniform(uniformHandler, "scale2_3", intervalCount, 1);
  uniformNames.scale4_5 = AddUniform(uniformHandler, "scale4_5", intervalCount, 2);
  uniformNames.scale6_7 = AddUniform(uniformHandler, "scale6_7", intervalCount, 3);
  uniformNames.scale8_9 = AddUniform(uniformHandler, "scale8_9", intervalCount, 4);
  uniformNames.scale10_11 = AddUniform(uniformHandler, "scale10_11", intervalCount, 5);
  uniformNames.scale12_13 = AddUniform(uniformHandler, "scale12_13", intervalCount, 6);
  uniformNames.scale14_15 = AddUniform(uniformHandler, "scale14_15", intervalCount, 7);
  uniformNames.bias0_1 = AddUniform(uniformHandler, "bias0_1", intervalCount, 0);
  uniformNames.bias2_3 = AddUniform(uniformHandler, "bias2_3", intervalCount, 1);
  uniformNames.bias4_5 = AddUniform(uniformHandler, "bias4_5", intervalCount, 2);
  uniformNames.bias6_7 = AddUniform(uniformHandler, "bias6_7", intervalCount, 3);
  uniformNames.bias8_9 = AddUniform(uniformHandler, "bias8_9", intervalCount, 4);
  uniformNames.bias10_11 = AddUniform(uniformHandler, "bias10_11", intervalCount, 5);
  uniformNames.bias12_13 = AddUniform(uniformHandler, "bias12_13", intervalCount, 6);
  uniformNames.bias14_15 = AddUniform(uniformHandler, "bias14_15", intervalCount, 7);
  uniformNames.thresholds1_7 = args.uniformHandler->addUniform(
      "thresholds1_7", UniformFormat::Float4, ShaderStage::Fragment);
  uniformNames.thresholds9_13 = args.uniformHandler->addUniform(
      "thresholds9_13", UniformFormat::Float4, ShaderStage::Fragment);

  fragBuilder->codeAppendf("float t = %s.x;", args.inputColor.c_str());
  fragBuilder->codeAppend("vec4 scale, bias;");
  fragBuilder->codeAppendf("// interval count: %d\n", intervalCount);

  if (intervalCount >= 4) {
    fragBuilder->codeAppend("// thresholds1_7.w is mid-point for intervals (0,7) and (8,15)\n");
    fragBuilder->codeAppendf("if (t < %s.w) {", uniformNames.thresholds1_7.c_str());
  }
  AppendCode1(fragBuilder, intervalCount, uniformNames);
  if (intervalCount > 4) {
    fragBuilder->codeAppend("} else {");
  }
  AppendCode2(fragBuilder, intervalCount, uniformNames);
  if (intervalCount >= 4) {
    fragBuilder->codeAppend("}");
  }

  fragBuilder->codeAppendf("%s = vec4(t * scale + bias);", args.outputColor.c_str());
}

void SetUniformData(UniformData* uniformData, const std::string& name, int intervalCount, int limit,
                    const Color& value) {
  if (intervalCount > limit) {
    uniformData->setData(name, value);
  }
}

void GLSLUnrolledBinaryGradientColorizer::onSetData(UniformData* /*vertexUniformData*/,
                                                    UniformData* fragmentUniformData) const {
  SetUniformData(fragmentUniformData, "scale0_1", intervalCount, 0, scale0_1);
  SetUniformData(fragmentUniformData, "scale2_3", intervalCount, 1, scale2_3);
  SetUniformData(fragmentUniformData, "scale4_5", intervalCount, 2, scale4_5);
  SetUniformData(fragmentUniformData, "scale6_7", intervalCount, 3, scale6_7);
  SetUniformData(fragmentUniformData, "scale8_9", intervalCount, 4, scale8_9);
  SetUniformData(fragmentUniformData, "scale10_11", intervalCount, 5, scale10_11);
  SetUniformData(fragmentUniformData, "scale12_13", intervalCount, 6, scale12_13);
  SetUniformData(fragmentUniformData, "scale14_15", intervalCount, 7, scale14_15);
  SetUniformData(fragmentUniformData, "bias0_1", intervalCount, 0, bias0_1);
  SetUniformData(fragmentUniformData, "bias2_3", intervalCount, 1, bias2_3);
  SetUniformData(fragmentUniformData, "bias4_5", intervalCount, 2, bias4_5);
  SetUniformData(fragmentUniformData, "bias6_7", intervalCount, 3, bias6_7);
  SetUniformData(fragmentUniformData, "bias8_9", intervalCount, 4, bias8_9);
  SetUniformData(fragmentUniformData, "bias10_11", intervalCount, 5, bias10_11);
  SetUniformData(fragmentUniformData, "bias12_13", intervalCount, 6, bias12_13);
  SetUniformData(fragmentUniformData, "bias14_15", intervalCount, 7, bias14_15);
  fragmentUniformData->setData("thresholds1_7", thresholds1_7);
  fragmentUniformData->setData("thresholds9_13", thresholds9_13);
}
}  // namespace tgfx
