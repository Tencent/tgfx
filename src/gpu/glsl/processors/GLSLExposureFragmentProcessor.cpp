/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "GLSLExposureFragmentProcessor.h"

namespace tgfx {
PlacementPtr<ExposureFragmentProcessor> ExposureFragmentProcessor::Make(BlockAllocator* allocator,
                                                                        float exposure) {
  return allocator->make<GLSLExposureFragmentProcessor>(exposure);
}

void GLSLExposureFragmentProcessor::emitCode(EmitArgs& args) const {
  auto uniformHandler = args.uniformHandler;
  auto exposureUniform =
      uniformHandler->addUniform("Exposure", UniformFormat::Float, ShaderStage::Fragment);

  auto fragBuilder = args.fragBuilder;
  // Unpremultiply alpha to get straight RGB.
  fragBuilder->codeAppendf("float alpha = %s.a;", args.inputColor.c_str());
  fragBuilder->codeAppend("vec3 color = vec3(0.0);");
  fragBuilder->codeAppend("if (alpha > 0.0) {");
  fragBuilder->codeAppendf("  color = %s.rgb / alpha;", args.inputColor.c_str());
  fragBuilder->codeAppend("}");

  fragBuilder->codeAppendf("float e = %s;", exposureUniform.c_str());

  // Convert sRGB to linear using gamma 2.2.
  fragBuilder->codeAppend("vec3 c = pow(max(color, vec3(0.0001)), vec3(2.2));");

  // Compute the tone-mapping multiplier using a power-of-10 curve.
  fragBuilder->codeAppend("float k = pow(10.0, 2.0 * e) - 1.0;");

  // Desaturate by 75% toward BT.709 luminance before tone mapping, then restore afterward. This
  // prevents oversaturation in highlights and preserves color relationships across channels.
  fragBuilder->codeAppend("const float DESAT = 0.75;");
  fragBuilder->codeAppend("float satRestore = 1.0 / (1.0 - DESAT) - 1.0;");
  fragBuilder->codeAppend("const vec3 LUMA_W = vec3(0.2126, 0.7152, 0.0722);");
  fragBuilder->codeAppend("float luma = dot(c, LUMA_W);");

  // For negative exposure, reduce the saturation restoration proportionally to luminance to avoid
  // color shifts in dark regions.
  fragBuilder->codeAppend("satRestore *= 1.0 + luma * luma * min(0.0, e) / (0.2 - min(0.0, e));");

  // Desaturate: blend each channel toward luminance.
  fragBuilder->codeAppend("c += (luma - c) * DESAT;");

  // For negative exposure, apply additional darkening.
  fragBuilder->codeAppend("if (e < 0.0) {");
  fragBuilder->codeAppend("  c *= 1.0 + e / 16.0;");
  fragBuilder->codeAppend("  luma *= 1.0 + e / 16.0;");
  fragBuilder->codeAppend("}");

  // Apply Reinhard tone mapping to both color and luminance.
  fragBuilder->codeAppend("c *= (k + 1.0) / (c * k + 1.0);");
  fragBuilder->codeAppend("luma *= (k + 1.0) / (luma * k + 1.0);");

  // Restore saturation.
  fragBuilder->codeAppend("c += (c - luma) * satRestore;");
  fragBuilder->codeAppend("c = clamp(c, 0.0, 1.0);");

  // Convert linear back to sRGB.
  fragBuilder->codeAppend("vec3 result = pow(c, vec3(1.0 / 2.2));");

  // Re-premultiply alpha.
  fragBuilder->codeAppendf("%s = vec4(result * alpha, alpha);", args.outputColor.c_str());
}

void GLSLExposureFragmentProcessor::onSetData(UniformData* /*vertexUniformData*/,
                                              UniformData* fragmentUniformData) const {
  fragmentUniformData->setData("Exposure", exposure);
}

}  // namespace tgfx
