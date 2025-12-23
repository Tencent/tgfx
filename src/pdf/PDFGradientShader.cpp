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

#include "PDFGradientShader.h"
#include <algorithm>
#include "core/ColorSpaceXformSteps.h"
#include "core/shaders/GradientShader.h"
#include "core/utils/ColorSpaceHelper.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "pdf/PDFDocumentImpl.h"
#include "pdf/PDFFormXObject.h"
#include "pdf/PDFGraphicState.h"
#include "pdf/PDFResourceDictionary.h"
#include "pdf/PDFTypes.h"
#include "pdf/PDFUtils.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Stream.h"
#include "tgfx/core/TileMode.h"
#include "tgfx/core/WriteStream.h"

namespace tgfx {

namespace {

uint32_t Hash(const GradientInfo& info) {
  std::hash<float> floatHasher;
  uint32_t hashValue = 0;

  for (const auto& color : info.colors) {
    hashValue ^= floatHasher(color.red) + 0x9e3779b9 + (hashValue << 6) + (hashValue >> 2);
    hashValue ^= floatHasher(color.green) + 0x9e3779b9 + (hashValue << 6) + (hashValue >> 2);
    hashValue ^= floatHasher(color.blue) + 0x9e3779b9 + (hashValue << 6) + (hashValue >> 2);
    hashValue ^= floatHasher(color.alpha) + 0x9e3779b9 + (hashValue << 6) + (hashValue >> 2);
  }
  for (const auto& position : info.positions) {
    hashValue ^= floatHasher(position) + 0x9e3779b9 + (hashValue << 6) + (hashValue >> 2);
  }
  for (const auto& point : info.points) {
    hashValue ^= floatHasher(point.x) + 0x9e3779b9 + (hashValue << 6) + (hashValue >> 2);
    hashValue ^= floatHasher(point.y) + 0x9e3779b9 + (hashValue << 6) + (hashValue >> 2);
  }
  for (const auto& radius : info.radiuses) {
    hashValue ^= floatHasher(radius) + 0x9e3779b9 + (hashValue << 6) + (hashValue >> 2);
  }

  return hashValue;
}

uint32_t Hash(const Matrix& matrix) {
  std::hash<float> floatHasher;
  uint32_t hashValue = 0;
  for (int i = 0; i < 6; i++) {
    hashValue ^= floatHasher(matrix[i]) + 0x9e3779b9 + (hashValue << 6) + (hashValue >> 2);
  }
  return hashValue;
}

uint32_t Hash(const Rect& rect) {
  std::hash<float> floatHasher;
  uint32_t hashValue = 0;
  hashValue ^= floatHasher(rect.left) + 0x9e3779b9 + (hashValue << 6) + (hashValue >> 2);
  hashValue ^= floatHasher(rect.top) + 0x9e3779b9 + (hashValue << 6) + (hashValue >> 2);
  hashValue ^= floatHasher(rect.right) + 0x9e3779b9 + (hashValue << 6) + (hashValue >> 2);
  hashValue ^= floatHasher(rect.bottom) + 0x9e3779b9 + (hashValue << 6) + (hashValue >> 2);
  return hashValue;
}

uint32_t Hash(const PDFGradientShader::Key& key) {
  uint32_t hashValue = Hash(key.info);
  hashValue ^= std::hash<int>{}(static_cast<int>(key.type)) + 0x9e3779b9 + (hashValue << 6) +
               (hashValue >> 2);
  hashValue ^= -Hash(key.canvasTransform);
  hashValue ^= -Hash(key.shaderTransform);
  hashValue ^= -Hash(key.boundBox);
  return hashValue;
}

///////////////////////////////////////////////////////////////////////////
Matrix UnitToPointsMatrix(const Point points[2]) {
  Point vec = points[1] - points[0];
  float mag = vec.length();
  float inv = mag != 0.f ? 1.f / mag : 0;

  vec *= inv;
  Matrix matrix;
  matrix.setSinCos(vec.y, vec.x);
  matrix.preScale(mag, mag);
  matrix.postTranslate(points[0].x, points[0].y);
  return matrix;
}

void TileModeCode(TileMode mode, const std::shared_ptr<MemoryWriteStream>& result) {
  if (mode == TileMode::Repeat) {
    result->writeText("dup truncate sub\n");     // Get the fractional part.
    result->writeText("dup 0 le {1 add} if\n");  // Map (-1,0) => (0,1)
    return;
  }

  if (mode == TileMode::Mirror) {
    // In Preview 11.0 (1033.3) `a n mod r eq` (with a and n both integers, r integer or real)
    // early aborts the function when false would be put on the stack.
    // Work around this by re-writing `t 2 mod 1 eq` as `t 2 mod 0 gt`.

    // Map t mod 2 into [0, 1, 1, 0].
    //                Code                 Stack t
    result->writeText(
        "abs "                 // +t
        "dup "                 // +t.s +t.s
        "truncate "            // +t.s +t
        "dup "                 // +t.s +t +t
        "cvi "                 // +t.s +t +T
        "2 mod "               // +t.s +t (+T mod 2)
                               /*"1 eq "*/
        "0 gt "                // +t.s +t true|false
        "3 1 roll "            // true|false +t.s +t
        "sub "                 // true|false 0.s
        "exch "                // 0.s true|false
        "{1 exch sub} if\n");  // 1 - 0.s|0.s
  }
}

/** 
 * Assumes t - startOffset is on the stack and does a linear interpolation on t between startOffset
 * and endOffset from prevColor to curColor (for each color component), leaving the result in
 * component order on the stack. It assumes there are always 3 components per color.
 */
void InterpolateColorCode(float range, Color beginColor, Color endColor,
                          const std::shared_ptr<MemoryWriteStream>& result) {
  // Linearly interpolate from the previous color to the current. Scale the colors from 0..255 to
  // 0..1 and determine the multipliers for interpolation.
  // C{r,g,b}(t, section) = t - offset_(section-1) + t * Multiplier{r,g,b}.

  // Figure out how to scale each color component.
  constexpr int ColorComponents = 3;
  float multiplier[ColorComponents];
  for (int i = 0; i < ColorComponents; i++) {
    multiplier[i] = (endColor[i] - beginColor[i]) / range;
  }

  // Calculate when we no longer need to keep a copy of the input parameter t. If the last component
  // to use t is i, then dupInput[0..i - 1] = true and dupInput[i .. components] = false.
  bool dupInput[ColorComponents];
  dupInput[ColorComponents - 1] = false;
  for (int i = ColorComponents - 2; i >= 0; i--) {
    dupInput[i] = dupInput[i + 1] || multiplier[i + 1] != 0;
  }

  if (!dupInput[0] && multiplier[0] == 0) {
    result->writeText("pop ");
  }

  for (int i = 0; i < ColorComponents; i++) {
    // If the next components needs t and this component will consume a
    // copy, make another copy.
    if (dupInput[i] && multiplier[i] != 0) {
      result->writeText("dup ");
    }

    auto colorComponent = static_cast<uint8_t>(beginColor[i] * 255);
    if (multiplier[i] == 0) {
      PDFUtils::AppendColorComponent(colorComponent, result);
      result->writeText(" ");
    } else {
      if (multiplier[i] != 1) {
        PDFUtils::AppendFloat(multiplier[i], result);
        result->writeText(" mul ");
      }
      if (colorComponent != 0) {
        PDFUtils::AppendColorComponent(colorComponent, result);
        result->writeText(" add ");
      }
    }

    if (dupInput[i]) {
      result->writeText("exch ");
    }
  }
}

void WriteGradientRanges(const GradientInfo& info, const std::vector<size_t>& rangeEnds, bool top,
                         bool first, const std::shared_ptr<MemoryWriteStream>& result) {
  DEBUG_ASSERT(!rangeEnds.empty());

  size_t rangeEndIndex = rangeEnds[rangeEnds.size() - 1];
  float rangeEnd = info.positions[rangeEndIndex];

  // Each range check tests 0 < t <= end.
  if (top) {
    DEBUG_ASSERT(first);
    // t may have been set to 0 to signal that the answer has already been found.
    result->writeText("dup dup 0 gt exch ");  // In Preview 11.0 (1033.3) `0. 0 ne` is true.
    PDFUtils::AppendFloat(rangeEnd, result);
    result->writeText(" le and {\n");
  } else if (first) {
    // After the top level check, only t <= end needs to be tested on if (lo) side.
    result->writeText("dup ");
    PDFUtils::AppendFloat(rangeEnd, result);
    result->writeText(" le {\n");
  } else {
    // The else (hi) side.
    result->writeText("{\n");
  }

  if (rangeEnds.size() == 1) {
    // Set the stack to [r g b].
    size_t rangeBeginIndex = rangeEndIndex - 1;
    float rangeBegin = info.positions[rangeBeginIndex];
    PDFUtils::AppendFloat(rangeBegin, result);
    result->writeText(" sub ");  // consume t, put t - startOffset on the stack.
    InterpolateColorCode(rangeEnd - rangeBegin, info.colors[rangeBeginIndex],
                         info.colors[rangeEndIndex], result);
    result->writeText("\n");
  } else {
    size_t lowCount = rangeEnds.size() / 2;
    std::vector<size_t> lowRanges(rangeEnds.begin(),
                                  rangeEnds.begin() + static_cast<std::ptrdiff_t>(lowCount));
    WriteGradientRanges(info, lowRanges, false, true, result);

    std::vector<size_t> highRanges(rangeEnds.begin() + static_cast<std::ptrdiff_t>(lowCount),
                                   rangeEnds.end());
    WriteGradientRanges(info, highRanges, false, false, result);
  }

  if (top) {
    // Put 0 on the stack for t once here instead of after every call to interpolate_color_code.
    result->writeText("0} if\n");
  } else if (first) {
    result->writeText("}");  // The else (hi) side will come next.
  } else {
    result->writeText("} ifelse\n");
  }
}

/* Generate Type 4 function code to map t to the passed gradient, clamping at the ends.
   The types integer, real, and boolean are available.
   There are no string, array, procedure, variable, or name types available.

   The generated code will be of the following form with all values hard coded.

  if (t <= 0) {
    ret = color[0];
    t = 0;
  }
  if (t > 0 && t <= stop[4]) {
    if (t <= stop[2]) {
      if (t <= stop[1]) {
        ret = interp(t - stop[0], stop[1] - stop[0], color[0], color[1]);
      } else {
        ret = interp(t - stop[1], stop[2] - stop[1], color[1], color[2]);
      }
    } else {
      if (t <= stop[3] {
        ret = interp(t - stop[2], stop[3] - stop[2], color[2], color[3]);
      } else {
        ret = interp(t - stop[3], stop[4] - stop[3], color[3], color[4]);
      }
    }
    t = 0;
  }
  if (t > 0) {
    ret = color[4];
  }

   which in PDF will be represented like

  dup 0 le {pop 0 0 0 0} if
  dup dup 0 gt exch 1 le and {
    dup .5 le {
      dup .25 le {
        0 sub 2 mul 0 0
      }{
        .25 sub .5 exch 2 mul 0
      } ifelse
    }{
      dup .75 le {
        .5 sub .5 exch .5 exch 2 mul
      }{
        .75 sub dup 2 mul .5 add exch dup 2 mul .5 add exch 2 mul .5 add
      } ifelse
    } ifelse
  0} if
  0 gt {1 1 1} if
 */
void GradientFunctionCode(const GradientInfo& info,
                          const std::shared_ptr<MemoryWriteStream>& result) {
  // While looking for a hit the stack is [t]. After finding a hit the stack is [r g b 0]. The 0 is
  // consumed just before returning.

  // The initial range has no previous and contains a solid color.
  // Any t <= 0 will be handled by this initial range, so later t == 0 indicates a hit was found.
  result->writeText("dup 0 le {pop ");
  PDFUtils::AppendColorComponent(static_cast<uint8_t>(info.colors[0].red * 255), result);
  result->writeText(" ");
  PDFUtils::AppendColorComponent(static_cast<uint8_t>(info.colors[0].green * 255), result);
  result->writeText(" ");
  PDFUtils::AppendColorComponent(static_cast<uint8_t>(info.colors[0].blue * 255), result);
  result->writeText(" 0} if\n");

  // Optimize out ranges which don't make any visual difference.
  std::vector<size_t> rangeEnds(info.colors.size());
  size_t rangeEndsCount = 0;
  for (size_t i = 1; i < info.colors.size(); ++i) {
    // Ignoring the alpha, is this range the same solid color as the next range?
    // This optimizes gradients where sometimes only the color or only the alpha is changing.
    auto eqIgnoringAlpha = [](Color a, Color b) {
      return FloatNearlyEqual(a.red, b.red) && FloatNearlyEqual(a.green, b.green) &&
             FloatNearlyEqual(a.blue, b.blue);
    };

    bool constantColorBothSides =
        eqIgnoringAlpha(info.colors[i - 1], info.colors[i]) &&  // This range is a solid color.
        i != info.colors.size() - 1 &&                          // This is not the last range.
        eqIgnoringAlpha(info.colors[i], info.colors[i + 1]);    // Next range is same solid color.

    // Does this range have zero size?
    bool degenerateRange = info.positions[i - 1] == info.positions[i];

    if (!degenerateRange && !constantColorBothSides) {
      rangeEnds[rangeEndsCount] = i;
      ++rangeEndsCount;
    }
  }

  // If a cap on depth is needed, loop here.
  std::vector<size_t> range(rangeEnds.begin(),
                            rangeEnds.begin() + static_cast<std::ptrdiff_t>(rangeEndsCount));
  WriteGradientRanges(info, range, true, true, result);

  // Clamp the final color.
  result->writeText("0 gt {");
  PDFUtils::AppendColorComponent(static_cast<uint8_t>(info.colors.back().red * 255), result);
  result->writeText(" ");
  PDFUtils::AppendColorComponent(static_cast<uint8_t>(info.colors.back().green * 255), result);
  result->writeText(" ");
  PDFUtils::AppendColorComponent(static_cast<uint8_t>(info.colors.back().blue * 255), result);
  result->writeText("} if\n");
}

void LinearCode(const GradientInfo& info, const Matrix& /*perspectiveRemover*/,
                const std::shared_ptr<MemoryWriteStream>& function) {
  function->writeText("{");

  function->writeText("pop\n");  // Just ditch the y value.
  TileModeCode(TileMode::Clamp, function);
  GradientFunctionCode(info, function);
  function->writeText("}");
}

void RadialCode(const GradientInfo& info, const Matrix& /*perspectiveRemover*/,
                const std::shared_ptr<MemoryWriteStream>& function) {
  function->writeText("{");

  // Find the distance from the origin.
  function->writeText(
      "dup "      // x y y
      "mul "      // x y^2
      "exch "     // y^2 x
      "dup "      // y^2 x x
      "mul "      // y^2 x^2
      "add "      // y^2+x^2
      "sqrt\n");  // sqrt(y^2+x^2)

  TileModeCode(TileMode::Clamp, function);
  GradientFunctionCode(info, function);
  function->writeText("}");
}

/**  
 * Conical gradient shader, based on the Canvas spec for radial gradients
 * See: http://www.w3.org/TR/2dcontext/#dom-context-2d-createradialgradient
 */
void TwoPointConicalCode(const GradientInfo& info, const Matrix& /*perspectiveRemover*/,
                         const std::shared_ptr<MemoryWriteStream>& function) {
  float dx = info.points[1].x - info.points[0].x;
  float dy = info.points[1].y - info.points[0].y;
  float r0 = info.radiuses[0];
  float dr = info.radiuses[1] - info.radiuses[0];
  float a = (dx * dx) + (dy * dy) - (dr * dr);

  // First compute t, if the pixel falls outside the cone, then we'll end
  // with 'false' on the stack, otherwise we'll push 'true' with t below it

  // We start with a stack of (x y), copy it and then consume one copy in
  // order to calculate b and the other to calculate c.
  function->writeText("{");

  function->writeText("2 copy ");

  // Calculate b and b^2; b = -2 * (y * dy + x * dx + r0 * dr).
  PDFUtils::AppendFloat(dy, function);
  function->writeText(" mul exch ");
  PDFUtils::AppendFloat(dx, function);
  function->writeText(" mul add ");
  PDFUtils::AppendFloat(r0 * dr, function);
  function->writeText(" add -2 mul dup dup mul\n");

  // c = x^2 + y^2 + radius0^2
  function->writeText("4 2 roll dup mul exch dup mul add ");
  PDFUtils::AppendFloat(r0 * r0, function);
  function->writeText(" sub dup 4 1 roll\n");

  // Contents of the stack at this point: c, b, b^2, c
  // if a = 0, then we collapse to a simpler linear case
  if (a == 0) {

    // t = -c/b
    function->writeText("pop pop div neg dup ");

    // compute radius(t)
    PDFUtils::AppendFloat(dr, function);
    function->writeText(" mul ");
    PDFUtils::AppendFloat(r0, function);
    function->writeText(" add\n");

    // if r(t) < 0, then it's outside the cone
    function->writeText("0 lt {pop false} {true} ifelse\n");

  } else {

    // quadratic case: the Canvas spec wants the largest
    // root t for which radius(t) > 0

    // compute the discriminant (b^2 - 4ac)
    PDFUtils::AppendFloat(a * 4, function);
    function->writeText(" mul sub dup\n");

    // if d >= 0, proceed
    function->writeText("0 ge {\n");

    // an intermediate value we'll use to compute the roots:
    // q = -0.5 * (b +/- sqrt(d))
    function->writeText("sqrt exch dup 0 lt {exch -1 mul} if");
    function->writeText(" add -0.5 mul dup\n");

    // first root = q / a
    PDFUtils::AppendFloat(a, function);
    function->writeText(" div\n");

    // second root = c / q
    function->writeText("3 1 roll div\n");

    // put the larger root on top of the stack
    function->writeText("2 copy gt {exch} if\n");

    // compute radius(t) for larger root
    function->writeText("dup ");
    PDFUtils::AppendFloat(dr, function);
    function->writeText(" mul ");
    PDFUtils::AppendFloat(r0, function);
    function->writeText(" add\n");

    // if r(t) > 0, we have our t, pop off the smaller root and we're done
    function->writeText(" 0 gt {exch pop true}\n");

    // otherwise, throw out the larger one and try the smaller root
    function->writeText("{pop dup\n");
    PDFUtils::AppendFloat(dr, function);
    function->writeText(" mul ");
    PDFUtils::AppendFloat(r0, function);
    function->writeText(" add\n");

    // if r(t) < 0, push false, otherwise the smaller root is our t
    function->writeText("0 le {pop false} {true} ifelse\n");
    function->writeText("} ifelse\n");

    // d < 0, clear the stack and push false
    function->writeText("} {pop pop pop false} ifelse\n");
  }

  // if the pixel is in the cone, proceed to compute a color
  function->writeText("{");
  TileModeCode(TileMode::Clamp, function);
  GradientFunctionCode(info, function);

  // otherwise, just write black
  // The "gradients" gm works as falls into the 8.7.4.5.4 "Type 3 (Radial) Shadings" case.
  function->writeText("} {0 0 0} ifelse }");
}

///////////////////////////////////////////////////////////////////////////

// catch cases where the inner just touches the outer circle
// and make the inner circle just inside the outer one to match raster
void FixUpRadius(const Point& p1, float& r1, const Point& p2, float& r2) {
  // detect touching circles
  auto distance = Point::Distance(p1, p2);
  auto subtractRadii = fabs(r1 - r2);
  if (fabs(distance - subtractRadii) < 0.002f) {
    if (r1 > r2) {
      r1 += 0.002f;
    } else {
      r2 += 0.002f;
    }
  }
}

PDFGradientShader::Key MakeKey(const GradientShader* gradientShader, const Matrix& canvasTransform,
                               const Rect& bbox) {
  PDFGradientShader::Key key = {GradientType::None,
                                {},
                                nullptr,
                                nullptr,
                                canvasTransform,
                                Matrix::I(),  // PDFUtils::GetShaderLocalMatrix(shader),
                                bbox,
                                0};
  key.type = gradientShader->asGradient(&key.info);
  DEBUG_ASSERT(key.type != GradientType::None);
  DEBUG_ASSERT(!key.info.colors.empty());
  key.colors = &key.info.colors;
  key.stops = &key.info.positions;
  key.hash = Hash(key);
  return key;
}

bool GradientHasAlpha(const PDFGradientShader::Key& key) {
  DEBUG_ASSERT(key.type != GradientType::None);
  return std::any_of(key.info.colors.begin(), key.info.colors.end(),
                     [](const auto& color) { return !FloatNearlyEqual(color.alpha, 1.0f); });
}

PDFGradientShader::Key CloneKey(const PDFGradientShader::Key& key) {
  PDFGradientShader::Key clone = {
      key.type,
      key.info,  // change pointers later.
      nullptr,  nullptr, key.canvasTransform, key.shaderTransform, key.boundBox, 0,
  };
  clone.colors = &clone.info.colors;
  clone.stops = &clone.info.positions;
  return clone;
}

PDFIndirectReference FindPDFShader(PDFDocumentImpl* doc, const PDFGradientShader::Key& key,
                                   bool keyHasAlpha);

std::unique_ptr<PDFDictionary> GetGradientResourceDictionary(PDFIndirectReference functionShader,
                                                             PDFIndirectReference gState) {
  std::vector<PDFIndirectReference> patternShaders;
  if (functionShader != PDFIndirectReference()) {
    patternShaders.push_back(functionShader);
  }
  std::vector<PDFIndirectReference> graphicStates;
  if (gState != PDFIndirectReference()) {
    graphicStates.push_back(gState);
  }
  return MakePDFResourceDictionary(std::move(graphicStates), std::move(patternShaders),
                                   std::vector<PDFIndirectReference>(),
                                   std::vector<PDFIndirectReference>());
}

std::shared_ptr<MemoryWriteStream> CreatePatternFillContent(int gsIndex, int patternIndex,
                                                            Rect& bounds) {
  auto content = MemoryWriteStream::Make();
  if (gsIndex >= 0) {
    PDFUtils::ApplyGraphicState(gsIndex, content);
  }
  PDFUtils::ApplyPattern(patternIndex, content);
  PDFUtils::AppendRectangle(bounds, content);
  PDFUtils::PaintPath(PathFillType::EvenOdd, content);
  return content;
}

PDFIndirectReference CreateSmaskGraphicState(PDFDocumentImpl* doc,
                                             const PDFGradientShader::Key& state) {
  PDFGradientShader::Key luminosityState = CloneKey(state);
  for (auto& color : luminosityState.info.colors) {
    color.alpha = 1.0f;
  }
  luminosityState.hash = Hash(luminosityState);

  DEBUG_ASSERT(!GradientHasAlpha(luminosityState));
  PDFIndirectReference luminosityShader = FindPDFShader(doc, std::move(luminosityState), false);
  auto resources = GetGradientResourceDictionary(luminosityShader, PDFIndirectReference());
  auto bbox = state.boundBox;
  auto contentStream = CreatePatternFillContent(-1, luminosityShader.value, bbox);
  auto contentData = contentStream->readData();
  auto alphaMask = MakePDFFormXObject(doc, contentData, PDFUtils::RectToArray(bbox),
                                      std::move(resources), Matrix::I(), "DeviceRGB");
  return PDFGraphicState::GetSMaskGraphicState(alphaMask, false,
                                               PDFGraphicState::SMaskMode::Luminosity, doc);
}

PDFIndirectReference MakeAlphaFunctionShader(PDFDocumentImpl* doc,
                                             const PDFGradientShader::Key& state) {
  PDFGradientShader::Key opaqueState = CloneKey(state);
  for (auto& color : opaqueState.info.colors) {
    color.alpha = 1.0f;
  }
  opaqueState.hash = Hash(opaqueState);

  DEBUG_ASSERT(!GradientHasAlpha(opaqueState));
  auto bbox = state.boundBox;
  PDFIndirectReference colorShader = FindPDFShader(doc, std::move(opaqueState), false);
  if (!colorShader) {
    return PDFIndirectReference();
  }
  // Create resource dict with alpha graphics state as G0 and
  // pattern shader as P0, then write content stream.
  PDFIndirectReference alphaGsRef = CreateSmaskGraphicState(doc, state);

  auto resourceDict = GetGradientResourceDictionary(colorShader, alphaGsRef);

  auto colorStream = CreatePatternFillContent(alphaGsRef.value, colorShader.value, bbox);
  auto alphaFunctionShader = PDFDictionary::Make();
  PDFUtils::PopulateTilingPatternDict(alphaFunctionShader.get(), bbox, std::move(resourceDict),
                                      Matrix::I());
  auto colorData = colorStream->readData();
  return PDFStreamOut(std::move(alphaFunctionShader), Stream::MakeFromData(colorData), doc);
}

std::unique_ptr<PDFDictionary> createInterpolationFunction(const Color& color1,
                                                           const Color& color2) {
  auto retval = PDFDictionary::Make();

  auto c0 = MakePDFArray();
  c0->appendColorComponent(static_cast<uint8_t>(color1.red * 255));
  c0->appendColorComponent(static_cast<uint8_t>(color1.green * 255));
  c0->appendColorComponent(static_cast<uint8_t>(color1.blue * 255));
  retval->insertObject("C0", std::move(c0));

  auto c1 = MakePDFArray();
  c1->appendColorComponent(static_cast<uint8_t>(color2.red * 255));
  c1->appendColorComponent(static_cast<uint8_t>(color2.green * 255));
  c1->appendColorComponent(static_cast<uint8_t>(color2.blue * 255));
  retval->insertObject("C1", std::move(c1));

  retval->insertObject("Domain", MakePDFArray(0, 1));

  retval->insertInt("FunctionType", 2);
  retval->insertScalar("N", 1.0f);

  return retval;
}

std::unique_ptr<PDFDictionary> GradientStitchCode(const GradientInfo& info) {
  auto retval = PDFDictionary::Make();

  // normalize color stops
  std::vector<Color> colors = info.colors;
  std::vector<float> colorOffsets = info.positions;

  size_t i = 1;
  auto colorCount = colors.size();
  while (i < colorCount - 1) {
    // ensure stops are in order
    colorOffsets[i] = std::max(colorOffsets[i - 1], colorOffsets[i]);

    // remove points that are between 2 coincident points
    if ((colorOffsets[i - 1] == colorOffsets[i]) && (colorOffsets[i] == colorOffsets[i + 1])) {
      colorCount -= 1;
      colors.erase(colors.begin() + static_cast<std::ptrdiff_t>(i));
      colorOffsets.erase(colorOffsets.begin() + static_cast<std::ptrdiff_t>(i));
    } else {
      i++;
    }
  }
  // find coincident points and slightly move them over
  for (i = 1; i < colorCount - 1; i++) {
    if (colorOffsets[i - 1] == colorOffsets[i]) {
      colorOffsets[i] += 0.00001f;
    }
  }
  // check if last 2 stops coincide
  if (colorOffsets[i - 1] == colorOffsets[i]) {
    colorOffsets[i - 1] -= 0.00001f;
  }

  // no need for a stitch function if there are only 2 stops.
  if (colorCount == 2) {
    return createInterpolationFunction(colors[0], colors[1]);
  }

  auto encode = MakePDFArray();
  auto bounds = MakePDFArray();
  auto functions = MakePDFArray();

  retval->insertObject("Domain", MakePDFArray(0, 1));
  retval->insertInt("FunctionType", 3);

  for (size_t index = 1; index < colorCount; index++) {
    if (index > 1) {
      bounds->appendScalar(colorOffsets[index - 1]);
    }

    encode->appendScalar(0);
    encode->appendScalar(1.0f);

    functions->appendObject(createInterpolationFunction(colors[index - 1], colors[index]));
  }

  retval->insertObject("Encode", std::move(encode));
  retval->insertObject("Bounds", std::move(bounds));
  retval->insertObject("Functions", std::move(functions));

  return retval;
}

PDFIndirectReference MakePSFunction(std::unique_ptr<Stream> psCode,
                                    std::unique_ptr<PDFArray> domain,
                                    std::unique_ptr<PDFObject> range, PDFDocumentImpl* document) {
  auto dict = PDFDictionary::Make();
  dict->insertInt("FunctionType", 4);
  dict->insertObject("Domain", std::move(domain));
  dict->insertObject("Range", std::move(range));
  return PDFStreamOut(std::move(dict), std::move(psCode), document);
}

PDFIndirectReference MakeFunctionShader(PDFDocumentImpl* doc, const PDFGradientShader::Key& state) {
  Point transformPoints[2];
  const GradientInfo& info = state.info;
  Matrix finalMatrix = state.canvasTransform;
  finalMatrix.preConcat(state.shaderTransform);

  bool doStitchFunctions =
      (state.type == GradientType::Linear || state.type == GradientType::Radial ||
       state.type == GradientType::Conic);

  enum class ShadingType : int32_t {
    Function = 1,
    Axial = 2,
    Radial = 3,
    FreeFormGouraudTriangleMesh = 4,
    LatticeFormGouraudTriangleMesh = 5,
    CoonsPatchMesh = 6,
    TensorProductPatchMesh = 7,
  };
  ShadingType shadingType;

  auto pdfShader = PDFDictionary::Make();
  if (doStitchFunctions) {
    pdfShader->insertObject("Function", GradientStitchCode(info));

    {  //default tile is clamp mode
      auto extend = MakePDFArray();
      extend->reserve(2);
      extend->appendBool(true);
      extend->appendBool(true);
      pdfShader->insertObject("Extend", std::move(extend));
    }

    std::unique_ptr<PDFArray> coords;
    switch (state.type) {
      case GradientType::Linear: {
        shadingType = ShadingType::Axial;
        const Point& pt1 = info.points[0];
        const Point& pt2 = info.points[1];
        coords = MakePDFArray(pt1.x, pt1.y, pt2.x, pt2.y);
      } break;
      case GradientType::Radial: {
        shadingType = ShadingType::Radial;
        const Point& pt1 = info.points[0];
        coords = MakePDFArray(pt1.x, pt1.y, 0, pt1.x, pt1.y, info.radiuses[0]);
      } break;
      case GradientType::Conic: {
        shadingType = ShadingType::Radial;
        float r1 = info.radiuses[0];
        float r2 = info.radiuses[1];
        Point pt1 = info.points[0];
        Point pt2 = info.points[1];
        FixUpRadius(pt1, r1, pt2, r2);

        coords = MakePDFArray(pt1.x, pt1.y, r1, pt2.x, pt2.y, r2);
        break;
      }
      case GradientType::None:
      default:
        DEBUG_ASSERT(false);
        return PDFIndirectReference();
    }
    pdfShader->insertObject("Coords", std::move(coords));
  } else {
    shadingType = ShadingType::Function;

    // Transform the coordinate space for the type of gradient.
    transformPoints[0] = info.points[0];
    transformPoints[1] = info.points[1];
    switch (state.type) {
      case GradientType::Linear:
        break;
      case GradientType::Radial:
        transformPoints[1] = transformPoints[0];
        transformPoints[1].x += info.radiuses[0];
        break;
      case GradientType::Conic: {
        transformPoints[1] = transformPoints[0];
        transformPoints[1].x += 1.0f;
        break;
      }
      case GradientType::None:
      default:
        return PDFIndirectReference();
    }

    // Move any scaling (assuming a unit gradient) or translation
    // (and rotation for linear gradient), of the final gradient from
    // info.fPoints to the matrix (updating bbox appropriately).  Now
    // the gradient can be drawn on on the unit segment.
    Matrix mapperMatrix = UnitToPointsMatrix(transformPoints);

    finalMatrix.preConcat(mapperMatrix);

    // Preserves as much as possible in the final matrix, and only removes
    // the perspective. The inverse of the perspective is stored in
    // perspectiveInverseOnly matrix and has 3 useful numbers
    // (p0, p1, p2), while everything else is either 0 or 1.
    // In this way the shader will handle it eficiently, with minimal code.

    auto perspectiveInverseOnly = Matrix::I();

    Rect bbox = state.boundBox;
    if (!PDFUtils::InverseTransformBBox(finalMatrix, &bbox)) {
      return PDFIndirectReference();
    }

    auto functionCode = MemoryWriteStream::Make();
    switch (state.type) {
      case GradientType::Linear:
        LinearCode(info, perspectiveInverseOnly, functionCode);
        break;
      case GradientType::Radial:
        RadialCode(info, perspectiveInverseOnly, functionCode);
        break;
      case GradientType::Conic: {
        // The two point radial gradient further references state.fInfo
        // in translating from x, y coordinates to the t parameter. So, we have
        // to transform the points and radii according to the calculated matrix.
        GradientInfo infoCopy = info;
        Matrix inverseMapperMatrix;
        if (!mapperMatrix.invert(&inverseMapperMatrix)) {
          return PDFIndirectReference();
        }
        inverseMapperMatrix.mapPoints(infoCopy.points.data(), 2);

        infoCopy.radiuses[0] =
            std::sqrt(inverseMapperMatrix.mapXY(info.radiuses[0], info.radiuses[0]).length());
        infoCopy.radiuses[1] =
            std::sqrt(inverseMapperMatrix.mapXY(info.radiuses[1], info.radiuses[1]).length());
        TwoPointConicalCode(infoCopy, perspectiveInverseOnly, functionCode);
      } break;
      default:
        DEBUG_ASSERT(false);
    }
    pdfShader->insertObject("Domain", MakePDFArray(bbox.left, bbox.right, bbox.top, bbox.bottom));

    auto domain = MakePDFArray(bbox.left, bbox.right, bbox.top, bbox.bottom);
    auto rangeObject = MakePDFArray(0, 1, 0, 1, 0, 1);
    auto functionStream = Stream::MakeFromData(functionCode->readData());
    pdfShader->insertRef("Function", MakePSFunction(std::move(functionStream), std::move(domain),
                                                    std::move(rangeObject), doc));
  }

  pdfShader->insertInt("ShadingType", static_cast<int>(shadingType));
  auto ref = doc->colorSpaceRef();
  if (ref) {
    pdfShader->insertRef("ColorSpace", ref);
  } else {
    pdfShader->insertName("ColorSpace", "DeviceRGB");
  }

  auto pdfFunctionShader = PDFDictionary::Make("Pattern");
  pdfFunctionShader->insertInt("PatternType", 2);
  pdfFunctionShader->insertObject("Matrix", PDFUtils::MatrixToArray(finalMatrix));
  pdfFunctionShader->insertObject("Shading", std::move(pdfShader));
  return doc->emit(*pdfFunctionShader);
}

PDFIndirectReference FindPDFShader(PDFDocumentImpl* doc, const PDFGradientShader::Key& key,
                                   bool keyHasAlpha) {
  DEBUG_ASSERT(GradientHasAlpha(key) == keyHasAlpha);
  PDFIndirectReference pdfShader;
  if (keyHasAlpha) {
    pdfShader = MakeAlphaFunctionShader(doc, key);
  } else {
    pdfShader = MakeFunctionShader(doc, key);
  }
  return pdfShader;
}

}  // namespace

PDFIndirectReference PDFGradientShader::Make(PDFDocumentImpl* doc, const GradientShader* shader,
                                             const Matrix& matrix, const Rect& surfaceBBox) {
  DEBUG_ASSERT(shader);

  PDFGradientShader::Key key = MakeKey(shader, matrix, surfaceBBox);
  if (NeedConvertColorSpace(ColorSpace::SRGB(), doc->dstColorSpace())) {
    ColorSpaceXformSteps steps{ColorSpace::SRGB().get(), AlphaType::Unpremultiplied,
                               doc->dstColorSpace().get(), AlphaType::Unpremultiplied};
    for (auto& color : key.info.colors) {
      steps.apply(color.array());
    }
  }
  bool alpha = GradientHasAlpha(key);
  return FindPDFShader(doc, std::move(key), alpha);
}

}  // namespace tgfx