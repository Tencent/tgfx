/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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
#include "svg/SVGAttributeParser.h"
#include "svg/SVGFilterContext.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/ImageFilter.h"

class SVGRenderContext;

namespace tgfx {

bool SVGFeColorMatrix::parseAndSetAttribute(const std::string& name, const std::string& value) {
  return INHERITED::parseAndSetAttribute(name, value) ||
         this->setType(SVGAttributeParser::parse<SVGFeColorMatrixType>("type", name, value)) ||
         this->setValues(SVGAttributeParser::parse<SVGFeColorMatrixValues>("values", name, value));
}

template <>
bool SVGAttributeParser::parse(SVGFeColorMatrixType* type) {
  static constexpr std::tuple<const char*, SVGFeColorMatrixType> typeMap[] = {
      {"matrix", SVGFeColorMatrixType::Matrix},
      {"saturate", SVGFeColorMatrixType::Saturate},
      {"hueRotate", SVGFeColorMatrixType::HueRotate},
      {"luminanceToAlpha", SVGFeColorMatrixType::LuminanceToAlpha},
  };

  return this->parseEnumMap(typeMap, type) && this->parseEOSToken();
}

ColorMatrix SVGFeColorMatrix::makeMatrixForType() const {
  if (Values.empty() && Type != SVGFeColorMatrixType::LuminanceToAlpha) {
    return ColorMatrix();
  }

  switch (Type) {
    case SVGFeColorMatrixType::Matrix: {
      if (Values.size() < 20) {
        return ColorMatrix();
      }
      ColorMatrix m;
      std::copy_n(Values.data(), 20, m.begin());
      return m;
    }
    case SVGFeColorMatrixType::Saturate:
      return MakeSaturate(!Values.empty() ? Values[0] : 1);
    case SVGFeColorMatrixType::HueRotate:
      return MakeHueRotate(!Values.empty() ? Values[0] : 0);
    case SVGFeColorMatrixType::LuminanceToAlpha:
      return MakeLuminanceToAlpha();
  }
}

ColorMatrix SVGFeColorMatrix::MakeSaturate(SVGNumberType sat) {
  enum {
    R_Scale = 0,
    G_Scale = 6,
    B_Scale = 12,
    A_Scale = 18,

    R_Trans = 4,
    G_Trans = 9,
    B_Trans = 14,
    A_Trans = 19,
  };

  constexpr float HueR = 0.213f;
  constexpr float HueG = 0.715f;
  constexpr float HueB = 0.072f;

  auto setrow = [](float row[], float r, float g, float b) {
    row[0] = r;
    row[1] = g;
    row[2] = b;
  };

  ColorMatrix matrix;

  matrix.fill(0.0f);
  float R = HueR * (1 - sat);
  float G = HueG * (1 - sat);
  float B = HueB * (1 - sat);
  setrow(matrix.data() + 0, R + sat, G, B);
  setrow(matrix.data() + 5, R, G + sat, B);
  setrow(matrix.data() + 10, R, G, B + sat);
  matrix[A_Scale] = 1;
  return matrix;
}

ColorMatrix SVGFeColorMatrix::MakeHueRotate(SVGNumberType degrees) {
  float theta = DegreesToRadians(degrees);
  SVGNumberType c = std::cos(theta);
  SVGNumberType s = std::sin(theta);
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

ColorMatrix SVGFeColorMatrix::MakeLuminanceToAlpha() {
  return ColorMatrix{0, 0, 0, 0, 0, 0,           0,           0,           0, 0,
                     0, 0, 0, 0, 0, LUM_COEFF_R, LUM_COEFF_G, LUM_COEFF_B, 0, 0};
}

std::shared_ptr<ImageFilter> SVGFeColorMatrix::onMakeImageFilter(
    const SVGRenderContext& context, const SVGFilterContext& filterContext) const {
  auto colorspace = resolveColorspace(context, filterContext);
  auto colorFilter = ImageFilter::ColorFilter(ColorFilter::Matrix(makeMatrixForType()));

  if (auto inputFilter = filterContext.resolveInput(context, this->getIn(), colorspace)) {
    colorFilter = ImageFilter::Compose(colorFilter, inputFilter);
  }
  return colorFilter;
}

}  // namespace tgfx
