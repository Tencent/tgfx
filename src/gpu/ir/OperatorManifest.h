/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License") you may not use this file except
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

#pragma once

#include <cstdint>
#include <vector>
#include "gpu/SLType.h"

namespace tgfx {

/// Stable operator identifiers. The values come from this list alone; do not assign them by hand
/// in call sites. The X-macro also derives the name lookup the build tool uses to resolve an
/// operator name (from the .inc annotation) to its id.
#define TGFX_OPERATOR_LIST(X)                                                                       \
  X(Luma, 1)                                                                                         \
  X(ConstColor, 2)                                                                                   \
  X(ColorMatrix, 3)                                                                                  \
  X(AlphaThreshold, 4)                                                                               \
  X(YUVToRGB, 5)                                                                                     \
  X(Clamp, 6)                                                                                        \
  X(DeviceCoord, 7)                                                                                  \
  X(MulAlpha, 8)                                                                                     \
  X(AlphaModulate, 9)                                                                                \
  X(GaussianBlur1D, 10)                                                                              \
  X(TiledSample1, 11)                                                                                \
  X(TiledSample2, 12)                                                                                \
  X(TiledSample4, 13)                                                                                \
  X(TiledSample1Rect, 14)                                                                            \
  X(TiledSample2Rect, 15)                                                                            \
  X(TiledSample4Rect, 16)                                                                            \
  X(TiledRepeatBlend, 17)                                                                            \
  X(TiledRepeatBlendRect, 18)                                                                        \
  X(TiledBorderLinear, 19)                                                                           \
  X(TiledBorderNearest, 20)                                                                          \
  X(NonAARRectCoverage, 21)                                                                          \
  X(ComplexRRectCoverage, 22)                                                                        \
  X(EllipseCoverage, 23)                                                                             \
  X(ComplexEllipseCoverage, 24)                                                                      \
  X(RRectCoverage, 25)                                                                               \
  X(RectCoverage, 26)                                                                                 \
  X(SingleGradientColor, 27)                                                                         \
  X(DualGradientColor, 28)                                                                           \
  X(LinearGradient, 29)                                                                              \
  X(RadialGradient, 30)                                                                              \
  X(DiamondGradient, 31)                                                                             \
  X(ConicGradient, 32)                                                                               \
  X(ClampedGradient, 33)                                                                             \
  X(HairlineLineCoverage, 34)                                                                       \
  X(HairlineQuadCoverage, 35)                                                                       \
  X(TextCoverageFromAlpha, 36)                                                                      \
  X(TextColorUnpremul, 37)                                                                          \
  X(ClampSubset, 38)                                                                               \
  X(KernelWeight, 39)                                                                               \
  X(KernelWeight2, 40)  \
  X(XpBlendClear, 41)  \
  X(XpBlendColor, 42)  \
  X(XpBlendColorBurn, 43)  \
  X(XpBlendColorDodge, 44)  \
  X(XpBlendDarken, 45)  \
  X(XpBlendDifference, 46)  \
  X(XpBlendDst, 47)  \
  X(XpBlendDstATop, 48)  \
  X(XpBlendDstIn, 49)  \
  X(XpBlendDstOut, 50)  \
  X(XpBlendDstOver, 51)  \
  X(XpBlendExclusion, 52)  \
  X(XpBlendHardLight, 53)  \
  X(XpBlendHue, 54)  \
  X(XpBlendLighten, 55)  \
  X(XpBlendLuminosity, 56)  \
  X(XpBlendModulate, 57)  \
  X(XpBlendMultiply, 58)  \
  X(XpBlendOverlay, 59)  \
  X(XpBlendPlusDarker, 60)  \
  X(XpBlendPlusLighter, 61) \
  X(XpBlendSaturation, 62)  \
  X(XpBlendScreen, 63)      \
  X(XpBlendSoftLight, 64)   \
  X(XpBlendSrc, 65)         \
  X(XpBlendSrcATop, 66)     \
  X(XpBlendSrcIn, 67)       \
  X(XpBlendSrcOut, 68)      \
  X(XpBlendSrcOver, 69)     \
  X(XpBlendXor, 70)         \
  X(GlassSDFRounded, 71)    \
  X(GlassSFDEllipse, 72)    \
  X(GlassRefraction, 73)    \
  X(GlassUDFTentBlur, 74)   \
  X(GlassUDFGeometry, 75)   \
  X(PerlinNoiseKernel, 76)  \
  X(TiledAxisCoord, 77)     \
  X(TiledAxisClamp, 78)     \
  X(TiledRepeatCoord, 79)   \
  X(RoundStrokeRectCoverage, 80) \
  X(ColorSpaceXform, 81)    \
  X(ShapeBlurRectCoverage, 82) \
  X(ShapeBlurRRectCoverageLow, 83) \
  X(ShapeBlurRRectCoverageHigh, 84) \
  X(ShapeMaskRectSDF, 85) \
  X(ShapeMaskRRectSDF, 86) \
  X(ShapeMaskCoverage, 87) \

enum class OperatorID : uint32_t {
#define TGFX_OPERATOR_ENUM(name, value) name = value,
  TGFX_OPERATOR_LIST(TGFX_OPERATOR_ENUM)
#undef TGFX_OPERATOR_ENUM
};

constexpr OperatorID INVALID_OPERATOR_ID = static_cast<OperatorID>(-1);

/// Resolves an operator name (the .inc annotation's spelling) to its id, or INVALID_OPERATOR_ID.
inline OperatorID OperatorIDFromName(const std::string& opName) {
#define TGFX_OPERATOR_CASE(name, value)                                                             \
  if (opName == #name) {                                                                            \
    return OperatorID::name;                                                                        \
  }
  TGFX_OPERATOR_LIST(TGFX_OPERATOR_CASE)
#undef TGFX_OPERATOR_CASE
  return INVALID_OPERATOR_ID;
}

struct OperatorParam {
  SLType type = SLType::Void;
  bool isArray = false;
  uint32_t arrayCount = 0;

  OperatorParam() = default;

  OperatorParam(SLType type) : type(type) {
  }

  OperatorParam(SLType type, bool isArray, uint32_t arrayCount)
      : type(type), isArray(isArray), arrayCount(arrayCount) {
  }
};

struct OperatorDesc {
  OperatorID id = INVALID_OPERATOR_ID;
  const char* debugName = nullptr;
  const char* glslSymbol = nullptr;
  SLType returnType = SLType::Void;
  std::vector<OperatorParam> params = {};
  std::vector<const char*> includes = {};
};

/// Returns the manifest entry for an operator, or null when the id is unknown.
const OperatorDesc* FindOperator(OperatorID id);

/// Iterates the manifest without exposing its storage. The build tool walks this list to emit one
/// SPIR-V module per operator, keyed by the operator id.
size_t OperatorCount();
const OperatorDesc* OperatorAt(size_t index);

}  // namespace tgfx
