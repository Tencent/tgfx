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

#include "core/CPUBlend.h"
#include <array>
#include "core/Blend.h"
#include "core/MathVector.h"
#include "utils/Log.h"
#include "utils/MathExtra.h"

namespace tgfx::CPUBlend {
enum class Component { Red, Green, Blue };

static float ColorGetComponent(const Color& color, Component component) {
  switch (component) {
    case Component::Red:
      return color.red;
    case Component::Green:
      return color.green;
    case Component::Blue:
      return color.blue;
    default:
      DEBUG_ASSERT(false);
      return 0.0f;
  }
}

static void ColorSetComponent(Color& color, Component component, float value) {
  switch (component) {
    case Component::Red:
      color.red = value;
      break;
    case Component::Green:
      color.green = value;
      break;
    case Component::Blue:
      color.blue = value;
      break;
    default:
      DEBUG_ASSERT(false);
      break;
  }
}

static void HardLight(const Color& srcColor, const Color& dstColor, Color& outColor) {
  for (int i = 0; i < 3; ++i) {
    if (2.0f * srcColor.array()[i] < srcColor.alpha) {
      outColor.array()[i] = 2.0f * srcColor.array()[i] * dstColor.array()[i];
    } else {
      const float part1 = srcColor.alpha * dstColor.alpha;
      const float part2 = 2.0f * (dstColor.alpha - dstColor.array()[i]);
      const float part3 = (srcColor.alpha - srcColor.array()[i]);
      outColor.array()[i] = part1 - part2 * part3;
    }
    const float part1 = srcColor.array()[i] * (1.0f - dstColor.alpha);
    const float part2 = dstColor.array()[i] * (1.0f - srcColor.alpha);
    outColor.array()[i] += part1 + part2;
  }
}

// Does one component of color-dodge
static void ColorDodgeComponent(const Color& srcColor, const Color& dstColor, Component component,
                                Color& outColor) {
  const float srcComponent = ColorGetComponent(srcColor, component);
  const float dstComponent = ColorGetComponent(dstColor, component);
  if (FloatNearlyZero(dstComponent)) {
    ColorSetComponent(outColor, component, srcComponent * (1.0f - dstColor.alpha));
  } else {
    if (float d = srcColor.alpha - srcComponent; FloatNearlyZero(d)) {
      const float part1 = srcColor.alpha * dstColor.alpha + srcComponent * (1.0f - dstColor.alpha);
      const float part2 = dstComponent * (1.0f - srcColor.alpha);
      ColorSetComponent(outColor, component, part1 + part2);
    } else {
      d = std::min(dstColor.alpha, dstComponent * srcColor.alpha / d);
      const float part1 = d * srcColor.alpha;
      const float part2 = srcComponent * (1.0f - dstColor.alpha);
      const float part3 = dstComponent * (1.0f - srcColor.alpha);
      ColorSetComponent(outColor, component, part1 + part2 + part3);
    }
  }
}

// Does one component of color-burn
static void ColorBurnComponent(const Color& srcColor, const Color& dstColor, Component component,
                               Color& outColor) {
  const float srcComponent = ColorGetComponent(srcColor, component);
  const float dstComponent = ColorGetComponent(dstColor, component);
  if (FloatNearlyEqual(dstColor.alpha, dstComponent)) {
    const float part1 = srcColor.alpha * dstColor.alpha;
    const float part2 = srcComponent * (1.0f - dstColor.alpha);
    const float part3 = dstComponent * (1.0f - srcColor.alpha);
    ColorSetComponent(outColor, component, part1 + part2 + part3);
  } else if (FloatNearlyZero(srcComponent)) {
    ColorSetComponent(outColor, component, dstComponent * (1.0f - srcColor.alpha));
  } else {
    const float numerator = dstColor.alpha - dstComponent;
    const float denominator = srcComponent;
    const float d = std::max(0.0f, dstColor.alpha - (numerator * srcColor.alpha / denominator));
    const float part1 = srcColor.alpha * d;
    const float part2 = srcComponent * (1.0f - dstColor.alpha);
    const float part3 = dstComponent * (1.0f - srcColor.alpha);
    ColorSetComponent(outColor, component, part1 + part2 + part3);
  }
}

// Does one component of soft-light. Caller should have already checked that dst alpha > 0.
static void SoftLightComponentPosDstAlpha(const Color& srcColor, const Color& dstColor,
                                          Color& outColor, Component component) {
  const float srcComponent = ColorGetComponent(srcColor, component);
  const float dstComponent = ColorGetComponent(dstColor, component);
  if (2.0f * srcComponent <= srcColor.alpha) {
    // (D^2 (Sa-2 S))/Da+(1-Da) S+D (-Sa+2 S+1)
    const float part1 =
        (dstComponent * dstComponent * (srcColor.alpha - 2.0f * srcComponent)) / dstColor.alpha;
    const float part2 = (1.0f - dstColor.alpha) * srcComponent;
    const float part3 = dstComponent * (-srcColor.alpha + 2.0f * srcComponent + 1.0f);
    ColorSetComponent(outColor, component, part1 + part2 + part3);
  } else if (4.0f * dstComponent <= dstColor.alpha) {
    const float dSqd = dstComponent * dstComponent;
    const float dCub = dSqd * dstComponent;
    const float daSqd = dstColor.alpha * dstColor.alpha;
    const float daCub = daSqd * dstColor.alpha;
    // (Da^3 (-S)+Da^2 (S-D (3 Sa-6 S-1))+12 Da D^2 (Sa-2 S)-16 D^3 (Sa-2 S))/Da^2
    const float part1 = daCub * (-srcComponent);
    const float part2 =
        daSqd *
        (srcComponent - dstComponent * (3.0f * srcColor.alpha - 6.0f * srcComponent - 1.0f));
    const float part3 = 12.0f * dstColor.alpha * dSqd * (srcColor.alpha - 2.0f * srcComponent);
    const float part4 = 16.0f * dCub * (srcColor.alpha - 2.0f * srcComponent);
    ColorSetComponent(outColor, component, (part1 + part2 + part3 - part4) / daSqd);
  } else {
    // -sqrt(Da * D) (Sa-2 S)-Da S+D (Sa-2 S+1)+S
    const float part1 =
        -sqrt(dstColor.alpha * dstComponent) * (srcColor.alpha - 2.0f * srcComponent);
    const float part2 = -dstColor.alpha * srcComponent;
    const float part3 = dstComponent * (srcColor.alpha - 2.0f * srcComponent + 1.0f);
    const float part4 = srcComponent;
    ColorSetComponent(outColor, component, part1 + part2 + part3 + part4);
  }
}

// Get the luminance of a color.
static float Luminance(const Vector3& color) {
  return 0.3f * color.x + 0.59f * color.y + 0.11f * color.z;
}

// Create a color which takes two colors and an alpha as input. It produces a color with the hue and saturation of the
// first color, the luminosity of the second color, and the input alpha.
static Vector3 MakeColorHueSatWithLuminance(const Vector3& hueSatColor, const Vector3& lumColor,
                                            const float alpha) {
  const float diff = Luminance(lumColor - hueSatColor);
  Vector3 outColor = hueSatColor + diff;
  const float outLum = Luminance(outColor);
  const float minComp = std::min(std::min(outColor.x, outColor.y), outColor.z);
  const float maxComp = std::max(std::max(outColor.x, outColor.y), outColor.z);
  if (minComp < 0.0f && !FloatNearlyEqual(outLum, minComp)) {
    outColor =
        ((outColor - Vector3(outLum, outLum, outLum)) * outLum) / (outLum - minComp) + outLum;
  }
  if (maxComp > alpha && !FloatNearlyEqual(maxComp, outLum)) {
    outColor =
        ((outColor - Vector3(outLum, outLum, outLum)) * (alpha - outLum)) / (maxComp - outLum) +
        outLum;
  }
  return outColor;
}

// Get the saturation of a color.
static float Saturation(const Vector3& color) {
  const float maxComponent = std::max(std::max(color.x, color.y), color.z);
  const float minComponent = std::min(std::min(color.x, color.y), color.z);
  return maxComponent - minComponent;
}

// Adjust the saturation given sorted input channels. This used to use inout params for min, mid, and max components
// but that seems to cause problems on PowerVR drivers. So instead it returns a vec3 where r, g ,b are the adjusted min,
// mid, and max inputs, respectively.
static Vector3 HueLumSatColorHelper(float minComp, float midComp, float maxComp, float sat) {
  if (minComp < maxComp) {
    Vector3 result;
    result.x = 0.0f;
    result.y = sat * (midComp - minComp) / (maxComp - minComp);
    result.z = sat;
    return result;
  } else {
    return {0.0f, 0.0f, 0.0f};
  }
}

// Create a color with the hue and luminosity of one input color and the saturation of another color.
static Vector3 MakeColorHueLumWithSaturation(const Vector3& hueLumColor, const Vector3& satColor) {
  std::array<float, 3> resultXYZ = {0.0f, 0.0f, 0.0f};
  const float sat = Saturation(satColor);
  if (hueLumColor.x <= hueLumColor.y) {
    if (hueLumColor.y <= hueLumColor.z) {
      const Vector3 tempColor =
          HueLumSatColorHelper(hueLumColor.x, hueLumColor.y, hueLumColor.z, sat);
      resultXYZ = {tempColor.x, tempColor.y, tempColor.z};
    } else if (hueLumColor.x <= hueLumColor.z) {
      const Vector3 tempColor =
          HueLumSatColorHelper(hueLumColor.x, hueLumColor.z, hueLumColor.y, sat);
      resultXYZ = {tempColor.x, tempColor.z, tempColor.y};
    } else {
      const Vector3 tempColor =
          HueLumSatColorHelper(hueLumColor.z, hueLumColor.x, hueLumColor.y, sat);
      resultXYZ = {tempColor.y, tempColor.z, tempColor.x};
    }
  } else if (hueLumColor.x <= hueLumColor.z) {
    const Vector3 tempColor =
        HueLumSatColorHelper(hueLumColor.y, hueLumColor.x, hueLumColor.z, sat);
    resultXYZ = {tempColor.y, tempColor.x, tempColor.z};
  } else if (hueLumColor.y <= hueLumColor.z) {
    const Vector3 tempColor =
        HueLumSatColorHelper(hueLumColor.y, hueLumColor.z, hueLumColor.x, sat);
    resultXYZ = {tempColor.z, tempColor.x, tempColor.y};
  } else {
    const Vector3 tempColor =
        HueLumSatColorHelper(hueLumColor.z, hueLumColor.y, hueLumColor.x, sat);
    resultXYZ = {tempColor.z, tempColor.y, tempColor.x};
  }
  return {resultXYZ[0], resultXYZ[1], resultXYZ[2]};
}

static void StandardBlendHandler_Overlay(const Color& srcColor, const Color& dstColor,
                                         Color& outColor) {
  // Overlay is Hard-Light with the src and dst reversed
  const Color blendSrcColor = dstColor;
  const Color blendDstColor = srcColor;
  HardLight(blendSrcColor, blendDstColor, outColor);
}

static void StandardBlendHandler_Darken(const Color& srcColor, const Color& dstColor,
                                        Color& outColor) {
  for (int i = 0; i < 3; ++i) {
    float part1 = (1.0f - srcColor.alpha) * dstColor.array()[i] + srcColor.array()[i];
    float part2 = (1.0f - dstColor.alpha) * srcColor.array()[i] + dstColor.array()[i];
    outColor.array()[i] = std::min(part1, part2);
  }
}

static void StandardBlendHandler_Lighten(const Color& srcColor, const Color& dstColor,
                                         Color& outColor) {
  for (int i = 0; i < 3; ++i) {
    float part1 = (1.0f - srcColor.alpha) * dstColor.array()[i] + srcColor.array()[i];
    float part2 = (1.0f - dstColor.alpha) * srcColor.array()[i] + dstColor.array()[i];
    outColor.array()[i] = std::max(part1, part2);
  }
}

static void StandardBlendHandler_ColorDodge(const Color& srcColor, const Color& dstColor,
                                            Color& outColor) {
  ColorDodgeComponent(srcColor, dstColor, Component::Red, outColor);
  ColorDodgeComponent(srcColor, dstColor, Component::Green, outColor);
  ColorDodgeComponent(srcColor, dstColor, Component::Blue, outColor);
}

static void StandardBlendHandler_ColorBurn(const Color& srcColor, const Color& dstColor,
                                           Color& outColor) {
  ColorBurnComponent(srcColor, dstColor, Component::Red, outColor);
  ColorBurnComponent(srcColor, dstColor, Component::Green, outColor);
  ColorBurnComponent(srcColor, dstColor, Component::Blue, outColor);
}

static void StandardBlendHandler_HardLight(const Color& srcColor, const Color& dstColor,
                                           Color& outColor) {
  HardLight(srcColor, dstColor, outColor);
}

static void StandardBlendHandler_SoftLight(const Color& srcColor, const Color& dstColor,
                                           Color& outColor) {
  if (FloatNearlyZero(dstColor.alpha)) {
    outColor = srcColor;
  } else {
    SoftLightComponentPosDstAlpha(srcColor, dstColor, outColor, Component::Red);
    SoftLightComponentPosDstAlpha(srcColor, dstColor, outColor, Component::Green);
    SoftLightComponentPosDstAlpha(srcColor, dstColor, outColor, Component::Blue);
  }
}

static void StandardBlendHandler_Difference(const Color& srcColor, const Color& dstColor,
                                            Color& outColor) {
  for (int i = 0; i < 3; ++i) {
    const float srcValue = srcColor.array()[i];
    const float dstValue = dstColor.array()[i];
    const float minProduct = std::min(srcValue * dstColor.alpha, dstValue * srcColor.alpha);
    outColor.array()[i] = srcValue + dstValue - 2.0f * minProduct;
  }
}

static void StandardBlendHandler_Exclusion(const Color& srcColor, const Color& dstColor,
                                           Color& outColor) {
  for (int i = 0; i < 3; i++) {
    const float srcValue = srcColor.array()[i];
    const float dstValue = dstColor.array()[i];
    outColor.array()[i] = srcValue + dstValue - 2.0f * srcValue * dstValue;
  }
}

static void StandardBlendHandler_Multiply(const Color& srcColor, const Color& dstColor,
                                          Color& outColor) {
  for (int i = 0; i < 3; ++i) {
    const float srcValue = srcColor.array()[i];
    const float dstValue = dstColor.array()[i];
    const float srcAlphaFactor = 1.0f - srcColor.alpha;
    const float dstAlphaFactor = 1.0f - dstColor.alpha;
    outColor.array()[i] =
        srcAlphaFactor * dstValue + dstAlphaFactor * srcValue + srcValue * dstValue;
  }
}

static void StandardBlendHandler_Hue(const Color& srcColor, const Color& dstColor,
                                     Color& outColor) {
  // SetLum(SetSat(S * Da, Sat(D * Sa)), Sa*Da, D*Sa) + (1 - Sa) * D + (1 - Da) * S
  const auto dstSrcAlpha = Color(dstColor.red * srcColor.alpha, dstColor.green * srcColor.alpha,
                                 dstColor.blue * srcColor.alpha, dstColor.alpha * srcColor.alpha);
  const Vector3 dstSrcAlphaRGB(dstSrcAlpha.red, dstSrcAlpha.green, dstSrcAlpha.blue);
  const Vector3 srcColorRGB(srcColor.red, srcColor.green, srcColor.blue);
  const Vector3 hueLumSatColorRGB =
      MakeColorHueLumWithSaturation(srcColorRGB * dstColor.alpha, dstSrcAlphaRGB);
  const Vector3 outputColorRGB =
      MakeColorHueSatWithLuminance(hueLumSatColorRGB, dstSrcAlphaRGB, dstSrcAlpha.alpha);
  const float outRGB[3] = {outputColorRGB.x, outputColorRGB.y, outputColorRGB.z};
  for (int i = 0; i < 3; ++i) {
    outColor.array()[i] = outRGB[i];
    outColor.array()[i] += (1.0f - srcColor.alpha) * dstColor.array()[i] +
                           (1.0f - dstColor.alpha) * srcColor.array()[i];
  }
}

static void StandardBlendHandler_Saturation(const Color& srcColor, const Color& dstColor,
                                            Color& outColor) {
  // SetLum(SetSat(D * Sa, Sat(S * Da)), Sa*Da, D*Sa)) + (1 - Sa) * D + (1 - Da) * S
  const auto dstSrcAlpha = Color(dstColor.red * srcColor.alpha, dstColor.green * srcColor.alpha,
                                 dstColor.blue * srcColor.alpha, dstColor.alpha * srcColor.alpha);
  const Vector3 dstSrcAlphaRGB(dstSrcAlpha.red, dstSrcAlpha.green, dstSrcAlpha.blue);
  const Vector3 srcColorRGB(srcColor.red, srcColor.green, srcColor.blue);
  const Vector3 hueLumSatColorRGB =
      MakeColorHueLumWithSaturation(dstSrcAlphaRGB, srcColorRGB * dstColor.alpha);
  const Vector3 outputColorRGB =
      MakeColorHueSatWithLuminance(hueLumSatColorRGB, dstSrcAlphaRGB, dstSrcAlpha.alpha);
  const float outRGB[3] = {outputColorRGB.x, outputColorRGB.y, outputColorRGB.z};
  for (int i = 0; i < 3; ++i) {
    outColor.array()[i] = outRGB[i];
    outColor.array()[i] += (1.0f - srcColor.alpha) * dstColor.array()[i] +
                           (1.0f - dstColor.alpha) * srcColor.array()[i];
  }
}

static void StandardBlendHandler_Color(const Color& srcColor, const Color& dstColor,
                                       Color& outColor) {
  // SetLum(S * Da, Sa* Da, D * Sa) + (1 - Sa) * D + (1 - Da) * S
  const Color srcDstAlpha(srcColor.red * dstColor.alpha, srcColor.green * dstColor.alpha,
                          srcColor.blue * dstColor.alpha, srcColor.alpha * dstColor.alpha);
  const Vector3 srcDstAlphaRGB(srcDstAlpha.red, srcDstAlpha.green, srcDstAlpha.blue);
  const Vector3 dstColorRGB(dstColor.red, dstColor.green, dstColor.blue);
  const Vector3 outputColorRGB =
      MakeColorHueSatWithLuminance(srcDstAlphaRGB, dstColorRGB * srcColor.alpha, srcDstAlpha.alpha);
  const float outRGB[3] = {outputColorRGB.x, outputColorRGB.y, outputColorRGB.z};
  for (int i = 0; i < 3; ++i) {
    outColor.array()[i] = outRGB[i];
    outColor.array()[i] += (1.0f - srcColor.alpha) * dstColor.array()[i] +
                           (1.0f - dstColor.alpha) * srcColor.array()[i];
  }
}

static void StandardBlendHandler_Luminosity(const Color& srcColor, const Color& dstColor,
                                            Color& outColor) {
  // SetLum(D * Sa, Sa* Da, S * Da) + (1 - Sa) * D + (1 - Da) * S
  const Color srcDstAlpha(srcColor.red * dstColor.alpha, srcColor.green * dstColor.alpha,
                          srcColor.blue * dstColor.alpha, srcColor.alpha * dstColor.alpha);
  const Vector3 srcDstAlphaRGB(srcDstAlpha.red, srcDstAlpha.green, srcDstAlpha.blue);
  const Vector3 dstColorRGB(dstColor.red, dstColor.green, dstColor.blue);
  const Vector3 outputColorRGB =
      MakeColorHueSatWithLuminance(dstColorRGB * srcColor.alpha, srcDstAlphaRGB, srcDstAlpha.alpha);
  const float outRGB[3] = {outputColorRGB.x, outputColorRGB.y, outputColorRGB.z};
  for (int i = 0; i < 3; ++i) {
    outColor.array()[i] = outRGB[i];
    outColor.array()[i] += (1.0f - srcColor.alpha) * dstColor.array()[i] +
                           (1.0f - dstColor.alpha) * srcColor.array()[i];
  }
}

static void StandardBlendHandler_PlusDarker(const Color& srcColor, const Color& dstColor,
                                            Color& outColor) {
  // MAX(0, (1 - ((Da * (1 - Dc) + Sa * (1 - Sc)))
  // https://developer.apple.com/documentation/coregraphics/cgblendmode/kcgblendmodeplusdarker
  for (int i = 0; i < 3; ++i) {
    outColor.array()[i] = std::clamp(
        1.0f + srcColor.array()[i] + dstColor.array()[i] - dstColor.alpha - srcColor.alpha, 0.0f,
        1.0f);
    outColor.array()[i] *= (outColor.alpha > 0.0f ? 1.0f : 0.0f);
  }
}

using StandardBlendHandler = void (*)(const Color& srcColor, const Color& dstColor,
                                      Color& outColor);

static constexpr std::pair<BlendMode, StandardBlendHandler> StandardBlendHandlers[] = {
    {BlendMode::Overlay, StandardBlendHandler_Overlay},
    {BlendMode::Darken, StandardBlendHandler_Darken},
    {BlendMode::Lighten, StandardBlendHandler_Lighten},
    {BlendMode::ColorDodge, StandardBlendHandler_ColorDodge},
    {BlendMode::ColorBurn, StandardBlendHandler_ColorBurn},
    {BlendMode::HardLight, StandardBlendHandler_HardLight},
    {BlendMode::SoftLight, StandardBlendHandler_SoftLight},
    {BlendMode::Difference, StandardBlendHandler_Difference},
    {BlendMode::Exclusion, StandardBlendHandler_Exclusion},
    {BlendMode::Multiply, StandardBlendHandler_Multiply},
    {BlendMode::Hue, StandardBlendHandler_Hue},
    {BlendMode::Saturation, StandardBlendHandler_Saturation},
    {BlendMode::Color, StandardBlendHandler_Color},
    {BlendMode::Luminosity, StandardBlendHandler_Luminosity},
    {BlendMode::PlusDarker, StandardBlendHandler_PlusDarker}};

static bool TryApplyStandardBlend(const Color& srcColor, const Color& dstColor, BlendMode blendMode,
                                  Color& outColor) {
  for (const auto& handler : StandardBlendHandlers) {
    if (handler.first == blendMode) {
      // All standard blending scenarios require performing src-over blending mode on the alpha channel.
      outColor.alpha = srcColor.alpha + (1.0f - srcColor.alpha) * dstColor.alpha;
      handler.second(srcColor, dstColor, outColor);
      return true;
    }
  }
  return false;
}

static bool TryApplyFormulaBlend(const Color& srcColor, const Color& dstColor, BlendMode blendMode,
                                 Color& outColor) {
  //TODO: implement formula blending
  if (srcColor.red == dstColor.red || blendMode == BlendMode::Clear) {
    outColor = srcColor;
    return true;
  }
  return false;
}

void Blend(const Color& srcColor, const Color& dstColor, BlendMode blendMode, Color& outColor) {
  if (TryApplyFormulaBlend(srcColor, dstColor, blendMode, outColor)) {
    return;
  }
  if (TryApplyStandardBlend(srcColor, dstColor, blendMode, outColor)) {
    return;
  }
  DEBUG_ASSERT(false);
}
}  // namespace tgfx::CPUBlend