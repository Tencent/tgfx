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

#include "GLSLColorCorrectionFragmentProcessor.h"

namespace tgfx {
PlacementPtr<ColorCorrectionFragmentProcessor> ColorCorrectionFragmentProcessor::Make(
    BlockAllocator* allocator, float exposure, float contrast, float saturation, float temperature,
    float tint, float highlights, float shadows) {
  return allocator->make<GLSLColorCorrectionFragmentProcessor>(
      exposure, contrast, saturation, temperature, tint, highlights, shadows);
}

void GLSLColorCorrectionFragmentProcessor::emitCode(EmitArgs& args) const {
  auto uniformHandler = args.uniformHandler;
  auto exposureUniform =
      uniformHandler->addUniform("Exposure", UniformFormat::Float, ShaderStage::Fragment);
  auto contrastUniform =
      uniformHandler->addUniform("Contrast", UniformFormat::Float, ShaderStage::Fragment);
  auto saturationUniform =
      uniformHandler->addUniform("Saturation", UniformFormat::Float, ShaderStage::Fragment);
  auto temperatureUniform =
      uniformHandler->addUniform("Temperature", UniformFormat::Float, ShaderStage::Fragment);
  auto tintUniform =
      uniformHandler->addUniform("Tint", UniformFormat::Float, ShaderStage::Fragment);
  auto highlightsUniform =
      uniformHandler->addUniform("Highlights", UniformFormat::Float, ShaderStage::Fragment);
  auto shadowsUniform =
      uniformHandler->addUniform("Shadows", UniformFormat::Float, ShaderStage::Fragment);

  auto fragBuilder = args.fragBuilder;

  // Unpremultiply alpha to get straight RGB.
  fragBuilder->codeAppendf("float alpha = %s.a;", args.inputColor.c_str());
  fragBuilder->codeAppend("vec3 color = vec3(0.0);");
  fragBuilder->codeAppend("if (alpha > 0.0) {");
  fragBuilder->codeAppendf("  color = %s.rgb / alpha;", args.inputColor.c_str());
  fragBuilder->codeAppend("}");

  // Load uniforms into local variables.
  fragBuilder->codeAppendf("float uExposure = %s;", exposureUniform.c_str());
  fragBuilder->codeAppendf("float uContrast = %s;", contrastUniform.c_str());
  fragBuilder->codeAppendf("float uSaturation = %s;", saturationUniform.c_str());
  fragBuilder->codeAppendf("float uTemperature = %s;", temperatureUniform.c_str());
  fragBuilder->codeAppendf("float uTint = %s;", tintUniform.c_str());
  fragBuilder->codeAppendf("float uHighlights = %s;", highlightsUniform.c_str());
  fragBuilder->codeAppendf("float uShadows = %s;", shadowsUniform.c_str());

  fragBuilder->codeAppend("const vec3 LUMA_W = vec3(0.2126, 0.7152, 0.0722);");
  fragBuilder->codeAppend("const float HS_THRESHOLD = 0.5;");

  // Convert sRGB to linear using gamma 2.2.
  fragBuilder->codeAppend("vec3 c = pow(max(color, vec3(0.0001)), vec3(2.2));");

  // ---- Step 1: Highlights & Shadows → compute per-pixel exposure compensation ----
  // Compute luminance offset from the threshold in linear space, then apply a sigmoid-like
  // compression to limit the adjustment range. Bright pixels (above threshold) are modulated by
  // highlights, dark pixels (below threshold) by shadows.
  fragBuilder->codeAppend("float hsLuma = dot(c, LUMA_W);");
  fragBuilder->codeAppend("float hsThreshLin = pow(HS_THRESHOLD, 2.2);");
  fragBuilder->codeAppend("float h = hsLuma - hsThreshLin;");
  fragBuilder->codeAppend(
      "float exposureBoost = 0.75 * h / sqrt(1.0 + 4.0 * h * h)"
      " * (h > 0.0 ? uHighlights : -uShadows * 0.999);");

  // ---- Step 2: Contrast + Temperature + Tint in CIE Lab space ----
  // Convert linear RGB to XYZ (sRGB D65 matrix).
  fragBuilder->codeAppend(
      "vec3 xyz = mat3("
      "0.4124564, 0.2126729, 0.0193339, "
      "0.3575761, 0.7151522, 0.1191920, "
      "0.1804375, 0.0721750, 0.9503041) * c;");

  // XYZ to Lab (simplified, no linear segment).
  fragBuilder->codeAppend("xyz.x /= 0.95047;");
  fragBuilder->codeAppend("xyz.z /= 1.08883;");
  fragBuilder->codeAppend("xyz = pow(max(xyz, vec3(0.0001)), vec3(1.0 / 3.0));");
  fragBuilder->codeAppend("vec3 lab;");
  fragBuilder->codeAppend("lab.x = 116.0 * xyz.y - 16.0;");
  fragBuilder->codeAppend("lab.y = 500.0 * (xyz.x - xyz.y);");
  fragBuilder->codeAppend("lab.z = 200.0 * (xyz.y - xyz.z);");

  // Temperature shifts the b* axis (blue-yellow), tint shifts the a* axis (green-magenta).
  fragBuilder->codeAppend("lab.y += 30.0 * uTint;");
  fragBuilder->codeAppend("lab.z += 30.0 * uTemperature;");

  // Contrast adjustment on L* channel using smootherstep for positive contrast and linear
  // compression for negative contrast. The threshold in Lab is HS_THRESHOLD * 100.
  fragBuilder->codeAppend("float labThreshold = HS_THRESHOLD * 100.0;");
  fragBuilder->codeAppend("float transWidth = 30.0;");
  fragBuilder->codeAppend("if (uContrast < 0.0) {");
  fragBuilder->codeAppend("  float target = (lab.x - labThreshold) * 0.2 + 50.0;");
  fragBuilder->codeAppend("  float amount = uContrast + 1.0;");
  fragBuilder->codeAppend("  lab.x = mix(target, lab.x, amount);");
  fragBuilder->codeAppend("} else if (uContrast > 0.0) {");
  // Smootherstep: 6t^5 - 15t^4 + 10t^3 (Ken Perlin's improved smoothstep).
  fragBuilder->codeAppend(
      "  float t = clamp((lab.x - (labThreshold - transWidth))"
      " / (2.0 * transWidth), 0.0, 1.0);");
  fragBuilder->codeAppend("  float stretched = t * t * t * (t * (t * 6.0 - 15.0) + 10.0) * 100.0;");
  fragBuilder->codeAppend("  lab.x = mix(lab.x, stretched, uContrast);");
  fragBuilder->codeAppend("}");

  // Lab to XYZ (inverse transform).
  fragBuilder->codeAppend("xyz.y = (lab.x + 16.0) / 116.0;");
  fragBuilder->codeAppend("xyz.x = lab.y / 500.0 + xyz.y;");
  fragBuilder->codeAppend("xyz.z = xyz.y - lab.z / 200.0;");
  fragBuilder->codeAppend("xyz = xyz * xyz * xyz;");
  fragBuilder->codeAppend("xyz.x *= 0.95047;");
  fragBuilder->codeAppend("xyz.z *= 1.08883;");

  // XYZ to linear RGB.
  fragBuilder->codeAppend(
      "c = mat3("
      "3.2404542, -0.9692660, 0.0556434, "
      "-1.5371385, 1.8760108, -0.2040259, "
      "-0.4985314, 0.0415560, 1.0572252) * xyz;");
  fragBuilder->codeAppend("c = clamp(c, 0.0, 1.0);");

  // ---- Step 3: Exposure with Reinhard tone mapping ----
  // Combine base exposure with the per-pixel highlight/shadow compensation.
  fragBuilder->codeAppend("float e = uExposure + exposureBoost;");
  fragBuilder->codeAppend("float k = pow(10.0, 2.0 * e) - 1.0;");

  // Desaturate by 75% toward luminance before tone mapping, then restore afterward. This prevents
  // oversaturation in highlights and preserves color relationships across channels.
  fragBuilder->codeAppend("const float DESAT = 0.75;");
  fragBuilder->codeAppend("float satRestore = 1.0 / (1.0 - DESAT) - 1.0;");
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

  // ---- Step 4: Saturation adjustment ----
  fragBuilder->codeAppend("float satLuma = dot(c, LUMA_W);");
  fragBuilder->codeAppend("if (uSaturation <= 0.0) {");
  // Desaturate: linear interpolation toward grayscale.
  fragBuilder->codeAppend("  c -= (satLuma - c) * uSaturation;");
  fragBuilder->codeAppend("} else {");
  // Increase saturation: compute a safe amplification factor that won't push channels out of
  // [0, 1], then blend toward the amplified color.
  fragBuilder->codeAppend(
      "  float minRatio = satLuma / (satLuma - min(min(c.r, c.g), c.b) + 1e-6);");
  fragBuilder->codeAppend(
      "  float maxRatio = (1.0 - satLuma) / (max(max(c.r, c.g), c.b) - satLuma + 1e-6);");
  fragBuilder->codeAppend("  float sk = mix(1.0, min(minRatio, maxRatio), uSaturation);");
  fragBuilder->codeAppend("  sk = min(sk, 2.0);");
  fragBuilder->codeAppend("  c += (satLuma - c) * (1.0 - sk);");
  fragBuilder->codeAppend("}");
  fragBuilder->codeAppend("c = clamp(c, 0.0, 1.0);");

  // Convert linear back to sRGB.
  fragBuilder->codeAppend("vec3 result = pow(c, vec3(1.0 / 2.2));");

  // Re-premultiply alpha.
  fragBuilder->codeAppendf("%s = vec4(result * alpha, alpha);", args.outputColor.c_str());
}

void GLSLColorCorrectionFragmentProcessor::onSetData(UniformData* /*vertexUniformData*/,
                                                     UniformData* fragmentUniformData) const {
  fragmentUniformData->setData("Exposure", exposure);
  fragmentUniformData->setData("Contrast", contrast);
  fragmentUniformData->setData("Saturation", saturation);
  fragmentUniformData->setData("Temperature", temperature);
  fragmentUniformData->setData("Tint", tint);
  fragmentUniformData->setData("Highlights", highlights);
  fragmentUniformData->setData("Shadows", shadows);
}

}  // namespace tgfx
