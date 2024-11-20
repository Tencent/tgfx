/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/svg/node/SVGFeColorMatrix.h"
#include <tuple>
#include "core/utils/MathExtra.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/svg/SVGAttributeParser.h"

class SVGRenderContext;

namespace tgfx {

bool SkSVGFeColorMatrix::parseAndSetAttribute(const char* name, const char* value) {
  return INHERITED::parseAndSetAttribute(name, value) ||
         this->setType(SVGAttributeParser::parse<SVGFeColorMatrixType>("type", name, value)) ||
         this->setValues(SVGAttributeParser::parse<SVGFeColorMatrixValues>("values", name, value));
}

template <>
bool SVGAttributeParser::parse(SVGFeColorMatrixType* type) {
  static constexpr std::tuple<const char*, SVGFeColorMatrixType> gTypeMap[] = {
      {"matrix", SVGFeColorMatrixType::kMatrix},
      {"saturate", SVGFeColorMatrixType::kSaturate},
      {"hueRotate", SVGFeColorMatrixType::kHueRotate},
      {"luminanceToAlpha", SVGFeColorMatrixType::kLuminanceToAlpha},
  };

  return this->parseEnumMap(gTypeMap, type) && this->parseEOSToken();
}

#ifndef RENDER_SVG
ColorMatrix SkSVGFeColorMatrix::makeMatrixForType() const {
  if (fValues.empty() && fType != SVGFeColorMatrixType::kLuminanceToAlpha) {
    return ColorMatrix();
  }

  switch (fType) {
    case SVGFeColorMatrixType::kMatrix: {
      if (fValues.size() < 20) {
        return ColorMatrix();
      }
      ColorMatrix m;
      std::copy_n(fValues.data(), 20, m.begin());
      return m;
    }
    case SVGFeColorMatrixType::kSaturate:
      return MakeSaturate(!fValues.empty() ? fValues[0] : 1);
    case SVGFeColorMatrixType::kHueRotate:
      return MakeHueRotate(!fValues.empty() ? fValues[0] : 0);
    case SVGFeColorMatrixType::kLuminanceToAlpha:
      return MakeLuminanceToAlpha();
  }
}

ColorMatrix SkSVGFeColorMatrix::MakeSaturate(SVGNumberType sat) {
  enum {
    kR_Scale = 0,
    kG_Scale = 6,
    kB_Scale = 12,
    kA_Scale = 18,

    kR_Trans = 4,
    kG_Trans = 9,
    kB_Trans = 14,
    kA_Trans = 19,
  };

  static const float kHueR = 0.213f;
  static const float kHueG = 0.715f;
  static const float kHueB = 0.072f;

  auto setrow = [](float row[], float r, float g, float b) {
    row[0] = r;
    row[1] = g;
    row[2] = b;
  };

  ColorMatrix matrix;
  // m.setSaturation(s);

  matrix.fill(0.0f);
  const float R = kHueR * (1 - sat);
  const float G = kHueG * (1 - sat);
  const float B = kHueB * (1 - sat);
  setrow(matrix.data() + 0, R + sat, G, B);
  setrow(matrix.data() + 5, R, G + sat, B);
  setrow(matrix.data() + 10, R, G, B + sat);
  matrix[kA_Scale] = 1;
  return matrix;
}

ColorMatrix SkSVGFeColorMatrix::MakeHueRotate(SVGNumberType degrees) {
  const float theta = DegreesToRadians(degrees);
  const SVGNumberType c = std::cos(theta);
  const SVGNumberType s = std::sin(theta);
  return ColorMatrix{0.213f + c * 0.787f + s * -0.213f,
                     0.715f + c * -0.715f + s * -0.715f,
                     0.072f + c * -0.072f + s * 0.928f,
                     0,
                     0,

                     0.213f + c * -0.213f + s * 0.143f,
                     0.715f + c * 0.285f + s * 0.140f,
                     0.072f + c * -0.072f + s * -0.283f,
                     0,
                     0,

                     0.213f + c * -0.213f + s * -0.787f,
                     0.715f + c * -0.715f + s * 0.715f,
                     0.072f + c * 0.928f + s * 0.072f,
                     0,
                     0,

                     0,
                     0,
                     0,
                     1,
                     0};
}

/** See ITU-R Recommendation BT.709 at http://www.itu.int/rec/R-REC-BT.709/ .*/
constexpr float LUM_COEFF_R = 0.2126f;
constexpr float LUM_COEFF_G = 0.7152f;
constexpr float LUM_COEFF_B = 0.0722f;

ColorMatrix SkSVGFeColorMatrix::MakeLuminanceToAlpha() {
  return ColorMatrix{0, 0, 0, 0, 0, 0,           0,           0,           0, 0,
                     0, 0, 0, 0, 0, LUM_COEFF_R, LUM_COEFF_G, LUM_COEFF_B, 0, 0};
}

std::shared_ptr<ImageFilter> SkSVGFeColorMatrix::onMakeImageFilter(
    const SVGRenderContext&, const SkSVGFilterContext&) const {
  return ImageFilter::ColorFilter(ColorFilter::Matrix(makeMatrixForType()));
}
#endif
}  // namespace tgfx