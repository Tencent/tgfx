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
#include <cmath>
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

void DiamondCode(const GradientInfo& info, const Matrix& /*perspectiveRemover*/,
                 const std::shared_ptr<MemoryWriteStream>& function) {
  function->writeText("{");

  // Compute t = max(|x|, |y|) for diamond gradient.
  function->writeText(
      "abs "     // x |y|
      "exch "    // |y| x
      "abs "     // |y| |x|
      "2 copy "  // |y| |x| |y| |x|
      "lt "      // |y| |x| (|y| < |x|)
      "{exch} "  // if |y| < |x|: swap to put max on top
      "if "      // max(|x|, |y|) min(|x|, |y|)
      "pop\n");  // max(|x|, |y|) = t

  TileModeCode(TileMode::Clamp, function);
  GradientFunctionCode(info, function);
  function->writeText("}");
}

///////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////
// Conic Gradient Triangle Mesh (ShadingType 4) Implementation
///////////////////////////////////////////////////////////////////////////

// Lower bound on the number of angular segments. 65 is chosen so that a full 360° sweep keeps
// each segment around 5.54 degrees. More segments improve arc accuracy but increase file size.
// Sweeps larger than 2π will use more segments according to the π/32 threshold at the call site.
constexpr int ConicSegmentCount = 65;
// Number of radial steps per segment for the rectangle strip. Fine subdivision is intentional:
// fewer steps leave large triangles whose shared edges can produce sub-pixel white-line artifacts
// in Chrome's Gouraud renderer.
// The trade-off is file size: 65 segments × 500 steps × 2 vertices ≈ 65K vertices per gradient.
constexpr int ConicRadialStepsPerSegment = 500;

Color InterpolateColorAtT(float t, const std::vector<Color>& colors,
                          const std::vector<float>& positions) {
  if (t <= positions.front()) {
    return colors.front();
  }
  if (t >= positions.back()) {
    return colors.back();
  }
  for (size_t i = 1; i < positions.size(); ++i) {
    if (t <= positions[i]) {
      float range = positions[i] - positions[i - 1];
      float localT = range > 0.f ? (t - positions[i - 1]) / range : 0.f;
      Color c1 = colors[i - 1];
      Color c2 = colors[i];
      return {c1.red + (c2.red - c1.red) * localT, c1.green + (c2.green - c1.green) * localT,
              c1.blue + (c2.blue - c1.blue) * localT, c1.alpha + (c2.alpha - c1.alpha) * localT};
    }
  }
  return colors.back();
}

void WriteBigEndianUint32(MemoryWriteStream* stream, uint32_t value) {
  uint8_t bytes[4];
  bytes[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
  bytes[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
  bytes[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
  bytes[3] = static_cast<uint8_t>(value & 0xFF);
  stream->write(bytes, 4);
}

void WriteTriangleMeshVertex(MemoryWriteStream* stream, uint8_t flag, float x, float y, float minX,
                             float maxX, float minY, float maxY, const Color& color) {
  stream->write(&flag, 1);
  float normalizedX = (x - minX) / (maxX - minX);
  float normalizedY = (y - minY) / (maxY - minY);
  // Use double arithmetic to avoid float-to-uint32 overflow. In float32, 1.0f * 4294967295.0f
  // rounds up to 4294967296.0f (exceeds UINT32_MAX), causing undefined behavior on cast.
  // WASM's i32.trunc_f64_u traps on out-of-range values, so we clamp in double first.
  static constexpr double COORD_MAX = 4294967295.0;
  auto coordX = static_cast<uint32_t>(std::clamp(
      static_cast<double>(std::clamp(normalizedX, 0.f, 1.f)) * COORD_MAX, 0.0, COORD_MAX));
  auto coordY = static_cast<uint32_t>(std::clamp(
      static_cast<double>(std::clamp(normalizedY, 0.f, 1.f)) * COORD_MAX, 0.0, COORD_MAX));
  WriteBigEndianUint32(stream, coordX);
  WriteBigEndianUint32(stream, coordY);
  auto r = static_cast<uint8_t>(std::clamp(color.red, 0.f, 1.f) * 255.f);
  auto g = static_cast<uint8_t>(std::clamp(color.green, 0.f, 1.f) * 255.f);
  auto b = static_cast<uint8_t>(std::clamp(color.blue, 0.f, 1.f) * 255.f);
  stream->write(&r, 1);
  stream->write(&g, 1);
  stream->write(&b, 1);
}

// Writes one wedge-shaped segment as a radial rectangle strip. Instead of a fan triangle
// (center + two rim points), each segment expands from the center outward using
// ConicRadialStepsPerSegment flag=1 vertex pairs. This eliminates seam artifacts because all
// inter-segment boundaries are shared radial edges, not arc edges.
//
// Vertex layout per segment:
//   3 degenerate flag=0 triangles (center, center, center) to reset the color anchor, then
//   ConicRadialStepsPerSegment+1 pairs of (leftEdge, rightEdge) points along the two radial
//   boundaries, stepping from radius=0 to radius=maxRadius.
void WriteConicRectStripSegment(MemoryWriteStream* stream, Point center, float maxRadius,
                                float fromAngle, float toAngle, const Color& fromColor,
                                const Color& toColor, float minX, float maxX, float minY,
                                float maxY) {
  Color blendColor = {
      (fromColor.red + toColor.red) * 0.5f, (fromColor.green + toColor.green) * 0.5f,
      (fromColor.blue + toColor.blue) * 0.5f, (fromColor.alpha + toColor.alpha) * 0.5f};

  // 3 degenerate triangles at center to establish color anchors (flag=0 each, area=0).
  WriteTriangleMeshVertex(stream, 0, center.x, center.y, minX, maxX, minY, maxY, blendColor);
  WriteTriangleMeshVertex(stream, 0, center.x, center.y, minX, maxX, minY, maxY, fromColor);
  WriteTriangleMeshVertex(stream, 0, center.x, center.y, minX, maxX, minY, maxY, toColor);

  // Radial rectangle strip: ConicRadialStepsPerSegment+1 pairs of points, each pair is the
  // left boundary (fromAngle direction) and right boundary (toAngle direction) at the same radius.
  // flag=1 extends the previous triangle by adding one new vertex: v0=prev[1], v1=prev[2], v2=new.
  // Alternating left/right produces a strip of quads (2 triangles per step).
  float cosFrom = std::cos(fromAngle);
  float sinFrom = std::sin(fromAngle);
  float cosTo = std::cos(toAngle);
  float sinTo = std::sin(toAngle);
  int steps = ConicRadialStepsPerSegment;
  for (int i = 0; i <= steps; ++i) {
    float r = maxRadius * static_cast<float>(i) / static_cast<float>(steps);
    float leftX = center.x + r * cosFrom;
    float leftY = center.y + r * sinFrom;
    float rightX = center.x + r * cosTo;
    float rightY = center.y + r * sinTo;
    WriteTriangleMeshVertex(stream, 1, leftX, leftY, minX, maxX, minY, maxY, fromColor);
    WriteTriangleMeshVertex(stream, 1, rightX, rightY, minX, maxX, minY, maxY, toColor);
  }
}

// Conic uses ShadingType 4 (Free-Form Gouraud-Shaded Triangle Mesh).
PDFIndirectReference MakeConicTriangleMeshShader(PDFDocumentImpl* doc,
                                                 const PDFGradientShader::Key& state) {
  const GradientInfo& info = state.info;
  Point center = info.points[0];
  float startAngleDeg = info.radiuses[0];
  float endAngleDeg = info.radiuses[1];
  float startAngleRad = startAngleDeg * static_cast<float>(M_PI) / 180.f;
  float endAngleRad = endAngleDeg * static_cast<float>(M_PI) / 180.f;
  float angleRange = endAngleRad - startAngleRad;
  int numSegments = std::max(ConicSegmentCount,
                             static_cast<int>(std::ceil(std::abs(angleRange) / (M_PI / 32.f))));
  float segmentAngle = angleRange / static_cast<float>(numSegments);

  Matrix finalMatrix = state.canvasTransform;
  finalMatrix.preConcat(state.shaderTransform);

  Rect bbox = state.boundBox;
  if (!PDFUtils::InverseTransformBBox(finalMatrix, &bbox)) {
    return PDFIndirectReference();
  }
  float maxRadius = std::max({Point::Distance(center, {bbox.left, bbox.top}),
                              Point::Distance(center, {bbox.right, bbox.top}),
                              Point::Distance(center, {bbox.left, bbox.bottom}),
                              Point::Distance(center, {bbox.right, bbox.bottom})});
  maxRadius *= 1.5f;

  float minX = center.x - maxRadius;
  float maxX = center.x + maxRadius;
  float minY = center.y - maxRadius;
  float maxY = center.y + maxRadius;

  auto meshStream = MemoryWriteStream::Make();

  Color firstColor = info.colors.front();
  Color lastColor = info.colors.back();
  float absSegAngle = std::abs(segmentAngle);
  float twoPi = static_cast<float>(2.0 * M_PI);
  float gapAngle = twoPi - std::abs(angleRange);
  if (gapAngle > absSegAngle * 0.5f) {
    float gapStart = endAngleRad;
    float gapEnd = angleRange > 0 ? (startAngleRad + twoPi) : (startAngleRad - twoPi);
    float hardEdge = angleRange > 0 ? twoPi : 0.f;
    float lastColorStart = std::min(gapStart, hardEdge);
    float lastColorEnd = std::max(gapStart, hardEdge);
    float firstColorStart = std::min(hardEdge, gapEnd);
    float firstColorEnd = std::max(hardEdge, gapEnd);
    if (std::abs(lastColorEnd - lastColorStart) > absSegAngle * 0.5f) {
      int segments = std::max(
          1, static_cast<int>(std::ceil(std::abs(lastColorEnd - lastColorStart) / absSegAngle)));
      float step = (lastColorEnd - lastColorStart) / static_cast<float>(segments);
      for (int i = 0; i < segments; ++i) {
        float a1 = lastColorStart + static_cast<float>(i) * step;
        float a2 = lastColorStart + static_cast<float>(i + 1) * step;
        WriteConicRectStripSegment(meshStream.get(), center, maxRadius, a1, a2, lastColor,
                                   lastColor, minX, maxX, minY, maxY);
      }
    }
    if (std::abs(firstColorEnd - firstColorStart) > absSegAngle * 0.5f) {
      int segments = std::max(
          1, static_cast<int>(std::ceil(std::abs(firstColorEnd - firstColorStart) / absSegAngle)));
      float step = (firstColorEnd - firstColorStart) / static_cast<float>(segments);
      for (int i = 0; i < segments; ++i) {
        float a1 = firstColorStart + static_cast<float>(i) * step;
        float a2 = firstColorStart + static_cast<float>(i + 1) * step;
        WriteConicRectStripSegment(meshStream.get(), center, maxRadius, a1, a2, firstColor,
                                   firstColor, minX, maxX, minY, maxY);
      }
    }
  }

  for (int seg = 0; seg < numSegments; ++seg) {
    float angle1 = startAngleRad + static_cast<float>(seg) * segmentAngle;
    float angle2 = startAngleRad + static_cast<float>(seg + 1) * segmentAngle;
    float t1 = static_cast<float>(seg) / static_cast<float>(numSegments);
    float t2 = static_cast<float>(seg + 1) / static_cast<float>(numSegments);

    Color color1 = InterpolateColorAtT(t1, info.colors, info.positions);
    Color color2 = InterpolateColorAtT(t2, info.colors, info.positions);
    WriteConicRectStripSegment(meshStream.get(), center, maxRadius, angle1, angle2, color1, color2,
                               minX, maxX, minY, maxY);
  }

  auto pdfShader = PDFDictionary::Make();
  pdfShader->insertInt("ShadingType", 4);
  auto ref = doc->colorSpaceRef();
  if (ref) {
    pdfShader->insertRef("ColorSpace", ref);
  } else {
    pdfShader->insertName("ColorSpace", "DeviceRGB");
  }
  pdfShader->insertInt("BitsPerCoordinate", 32);
  pdfShader->insertInt("BitsPerComponent", 8);
  pdfShader->insertInt("BitsPerFlag", 8);
  auto decode = MakePDFArray(minX, maxX, minY, maxY, 0, 1, 0, 1, 0, 1);
  pdfShader->insertObject("Decode", std::move(decode));

  auto shadingRef =
      PDFStreamOut(std::move(pdfShader), Stream::MakeFromData(meshStream->readData()), doc);

  auto pdfPattern = PDFDictionary::Make("Pattern");
  pdfPattern->insertInt("PatternType", 2);
  pdfPattern->insertObject("Matrix", PDFUtils::MatrixToArray(finalMatrix));
  pdfPattern->insertRef("Shading", shadingRef);
  return doc->emit(*pdfPattern);
}

// Linear and Radial use native PDF Shading support (ShadingType 2/3).
// Diamond uses ShadingType 1 (Function-based) with PostScript code.
PDFIndirectReference MakeFunctionShader(PDFDocumentImpl* doc, const PDFGradientShader::Key& state) {
  Point transformPoints[2];
  const GradientInfo& info = state.info;
  Matrix finalMatrix = state.canvasTransform;
  finalMatrix.preConcat(state.shaderTransform);

  bool doStitchFunctions =
      (state.type == GradientType::Linear || state.type == GradientType::Radial);

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
      case GradientType::Diamond: {
        // DiamondRadiusToUnitMatrix applies translate + scale(sqrt(2)/r) + rotate(45).
        // UnitToPointsMatrix must produce its inverse: rotate(-45) + scale(r/sqrt(2)) + translate.
        // Setting vec = (r/2, -r/2) gives |vec| = r/sqrt(2) and angle = -45°.
        float halfR = info.radiuses[0] * 0.5f;
        transformPoints[1] = {transformPoints[0].x + halfR, transformPoints[0].y - halfR};
      } break;
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
      case GradientType::Diamond:
        DiamondCode(info, perspectiveInverseOnly, functionCode);
        break;
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
  if (keyHasAlpha) {
    return MakeAlphaFunctionShader(doc, key);
  }
  if (key.type == GradientType::Conic) {
    return MakeConicTriangleMeshShader(doc, key);
  }
  return MakeFunctionShader(doc, key);
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
