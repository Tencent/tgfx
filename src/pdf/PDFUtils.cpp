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

#include "PDFUtils.h"
#include <array>
#include <cfloat>
#include <string>
#include "pdf/PDFResourceDictionary.h"
#include "pdf/PDFTypes.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/UTF.h"
#include "tgfx/core/WriteStream.h"

namespace tgfx {

namespace {

size_t PrintPermilAsDecimal(int x, char* result, int places) {
  result[0] = '.';
  for (int i = places; i > 0; --i) {
    result[i] = static_cast<char>(x % 10);
    x /= 10;
  }
  size_t j = 0;
  for (j = static_cast<size_t>(places); j > 1; --j) {
    if (result[j] != '0') {
      break;
    }
  }
  result[j + 1] = '\0';
  return j + 1;
}

constexpr int IntPow(int base, unsigned exp, int acc = 1) {
  return exp < 1 ? acc : IntPow(base * base, exp / 2, (exp % 2) ? acc * base : acc);
}

std::array<float, 6> MatrixToPDFAffine(const Matrix& matrix) {
  std::array<float, 6> origin = {};
  matrix.get6(origin.data());

  std::array<float, 6> affine = {};
  constexpr int AFFINE_SCALE_X = 0;
  constexpr int AFFINE_SKEW_Y = 1;
  constexpr int AFFINE_SKEW_X = 2;
  constexpr int AFFINE_SCALE_Y = 3;
  constexpr int AFFINE_TRANS_X = 4;
  constexpr int AFFINE_TRANS_Y = 5;

  affine[AFFINE_SCALE_X] = origin[0];  // Matrix::SCALE_X
  affine[AFFINE_SKEW_Y] = origin[3];   // Matrix::SKEW_Y
  affine[AFFINE_SKEW_X] = origin[1];   // Matrix::SKEW_X
  affine[AFFINE_SCALE_Y] = origin[4];  // Matrix::SCALE_Y
  affine[AFFINE_TRANS_X] = origin[2];  // Matrix::TRANS_X
  affine[AFFINE_TRANS_Y] = origin[5];  // Matrix::TRANS_Y
  return affine;
}
}  // namespace

std::unique_ptr<PDFArray> PDFUtils::RectToArray(const Rect& rect) {
  return MakePDFArray(rect.left, rect.top, rect.right, rect.bottom);
}

std::unique_ptr<PDFArray> PDFUtils::MatrixToArray(const Matrix& matrix) {
  auto affine = MatrixToPDFAffine(matrix);
  return MakePDFArray(affine[0], affine[1], affine[2], affine[3], affine[4], affine[5]);
}

size_t PDFUtils::ColorToDecimal(uint8_t value, char result[5]) {
  if (value == 255 || value == 0) {
    result[0] = value ? '1' : '0';
    result[1] = '\0';
    return 1;
  }
  int x = static_cast<int>(std::round((1000.0 / 255.0) * value));
  return PrintPermilAsDecimal(x, result, 3);
}

size_t PDFUtils::ColorToDecimal(float value, char result[6]) {
  constexpr int factor = IntPow(10, 4);
  int x = static_cast<int>(std::round(value * factor));
  if (x >= factor || x <= 0) {  // clamp to 0-1
    result[0] = x > 0 ? '1' : '0';
    result[1] = '\0';
    return 1;
  }
  return PrintPermilAsDecimal(x, result, 4);
}

void PDFUtils::GetDateTime(DateTime* dataTime) {
  if (dataTime) {
    time_t m_time;
    time(&m_time);
    tm tstruct;
#ifdef _WIN32
    gmtime_s(&tstruct, &m_time);
#else
    gmtime_r(&m_time, &tstruct);
#endif

    dataTime->timeZoneMinutes = 0;
    dataTime->year = static_cast<uint16_t>(tstruct.tm_year + 1900);
    dataTime->month = static_cast<uint8_t>(tstruct.tm_mon + 1);
    dataTime->dayOfWeek = static_cast<uint8_t>(tstruct.tm_wday);
    dataTime->day = static_cast<uint8_t>(tstruct.tm_mday);
    dataTime->hour = static_cast<uint8_t>(tstruct.tm_hour);
    dataTime->minute = static_cast<uint8_t>(tstruct.tm_min);
    dataTime->second = static_cast<uint8_t>(tstruct.tm_sec);
  }
}

void PDFUtils::AppendColorComponent(uint8_t value, const std::shared_ptr<WriteStream>& stream) {
  char buffer[5];
  size_t len = ColorToDecimal(value, buffer);
  stream->write(buffer, len);
}

void PDFUtils::AppendColorComponent(float value, const std::shared_ptr<WriteStream>& stream) {
  char buffer[7];
  size_t len = ColorToDecimal(value, buffer);
  stream->write(buffer, len);
}

void PDFUtils::WriteUInt8(const std::shared_ptr<WriteStream>& stream, uint8_t value) {
  char result[2] = {HexadecimalDigits::upper[value >> 4], HexadecimalDigits::upper[value & 0xF]};
  stream->write(result, 2);
}

void PDFUtils::WriteUInt16BE(const std::shared_ptr<WriteStream>& stream, uint16_t value) {
  char result[4] = {
      HexadecimalDigits::upper[value >> 12], HexadecimalDigits::upper[0xF & (value >> 8)],
      HexadecimalDigits::upper[0xF & (value >> 4)], HexadecimalDigits::upper[0xF & (value)]};
  stream->write(result, 4);
}

void PDFUtils::WriteUTF16beHex(const std::shared_ptr<WriteStream>& stream, Unichar utf32) {
  auto UTF16String = UTF::ToUTF16(utf32);
  WriteUInt16BE(stream, static_cast<uint16_t>(UTF16String[0]));
  if (UTF16String.size() == 2) {
    WriteUInt16BE(stream, static_cast<uint16_t>(UTF16String[1]));
  }
}

void PDFUtils::AppendFloat(float value, const std::shared_ptr<WriteStream>& stream) {
  if (value == INFINITY) {
    value = FLT_MAX;
  }
  if (value == -INFINITY) {
    value = -FLT_MAX;
  }
  if (!std::isfinite(value)) {
    value = 0.0f;  // PDF does not support NaN, so we use 0.0f as a fallback.
  }
  auto result = std::to_string(value);
  stream->write(result.data(), result.size());
}

void PDFUtils::AppendTransform(const Matrix& matrix, const std::shared_ptr<WriteStream>& stream) {
  auto affine = MatrixToPDFAffine(matrix);
  for (float value : affine) {
    PDFUtils::AppendFloat(value, stream);
    stream->writeText(" ");
  }
  stream->writeText("cm\n");
}

void PDFUtils::AppendRectangle(const Rect& rect, const std::shared_ptr<WriteStream>& content) {
  float bottom = std::min(rect.bottom, rect.top);
  PDFUtils::AppendFloat(rect.left, content);
  content->writeText(" ");
  PDFUtils::AppendFloat(bottom, content);
  content->writeText(" ");
  PDFUtils::AppendFloat(rect.width(), content);
  content->writeText(" ");
  PDFUtils::AppendFloat(rect.height(), content);
  content->writeText(" re\n");
}

void PDFUtils::ApplyGraphicState(int objectIndex, const std::shared_ptr<WriteStream>& content) {
  PDFWriteResourceName(content, PDFResourceType::ExtGState, objectIndex);
  content->writeText(" gs\n");
}

void PDFUtils::ApplyPattern(int objectIndex, const std::shared_ptr<WriteStream>& content) {
  // Select Pattern color space (CS, cs) and set pattern object as current
  // color (SCN, scn)
  content->writeText("/Pattern CS/Pattern cs");
  PDFWriteResourceName(content, PDFResourceType::Pattern, objectIndex);
  content->writeText(" SCN");
  PDFWriteResourceName(content, PDFResourceType::Pattern, objectIndex);
  content->writeText(" scn\n");
}

namespace {
bool AllPointsEqual(const Point pts[], int count) {
  for (int i = 1; i < count; ++i) {
    if (pts[0] != pts[i]) {
      return false;
    }
  }
  return true;
}

void MoveTo(float x, float y, const std::shared_ptr<WriteStream>& content) {
  PDFUtils::AppendFloat(x, content);
  content->writeText(" ");
  PDFUtils::AppendFloat(y, content);
  content->writeText(" m\n");
}

void AppendLine(float x, float y, const std::shared_ptr<WriteStream>& content) {
  PDFUtils::AppendFloat(x, content);
  content->writeText(" ");
  PDFUtils::AppendFloat(y, content);
  content->writeText(" l\n");
}

void AppendCubic(float ctl1X, float ctl1Y, float ctl2X, float ctl2Y, float dstX, float dstY,
                 const std::shared_ptr<WriteStream>& content) {
  std::string cmd("y\n");
  PDFUtils::AppendFloat(ctl1X, content);
  content->writeText(" ");
  PDFUtils::AppendFloat(ctl1Y, content);
  content->writeText(" ");
  if (ctl2X != dstX || ctl2Y != dstY) {
    cmd = "c\n";
    PDFUtils::AppendFloat(ctl2X, content);
    content->writeText(" ");
    PDFUtils::AppendFloat(ctl2Y, content);
    content->writeText(" ");
  }
  PDFUtils::AppendFloat(dstX, content);
  content->writeText(" ");
  PDFUtils::AppendFloat(dstY, content);
  content->writeText(" ");
  content->writeText(cmd);
}

void ConvertQuadToCubic(const Point src[3], Point dst[4]) {
  auto scale = static_cast<float>(2.0 / 3.0);
  dst[0] = src[0];
  dst[1] = src[0] + (src[1] - src[0]) * scale;
  dst[2] = src[2] + (src[1] - src[2]) * scale;
  dst[3] = src[2];
}

void AppendQuad(const Point quad[], const std::shared_ptr<WriteStream>& content) {
  Point cubic[4];
  ConvertQuadToCubic(quad, cubic);
  AppendCubic(cubic[1].x, cubic[1].y, cubic[2].x, cubic[2].y, cubic[3].x, cubic[3].y, content);
}

void ClosePath(const std::shared_ptr<WriteStream>& content) {
  content->writeText("h\n");
}

}  // namespace

void PDFUtils::EmitPath(const Path& path, bool doConsumeDegerates,
                        const std::shared_ptr<MemoryWriteStream>& content) {
  if (path.isEmpty()) {
    PDFUtils::AppendRectangle({0, 0, 0, 0}, content);
    return;
  }
  // Filling a path with no area results in a drawing in PDF renderers but
  // Chrome expects to be able to draw some such entities with no visible
  // result, so we detect those cases and discard the drawing for them.
  // Specifically: moveTo(X), lineTo(Y) and moveTo(X), lineTo(X), lineTo(Y).

  auto rect = Rect::MakeEmpty();
  bool isClosed = true;  // Both closure and direction need to be checked.
  bool isReversed = false;
  if (path.isRect(&rect, &isClosed, &isReversed) && isClosed &&
      (!isReversed || PathFillType::EvenOdd == path.getFillType())) {
    PDFUtils::AppendRectangle(rect, content);
    return;
  }

  enum class SkipFillState {
    Empty,
    SingleLine,
    NonSingleLine,
  };

  auto fillState = SkipFillState::Empty;
  auto lastMovePt = Point::Make(0, 0);
  auto currentSegment = MemoryWriteStream::Make();

  auto pathIterator = [&](PathVerb verb, const Point points[4], void* /*info*/) -> void {
    switch (verb) {
      case PathVerb::Move:
        MoveTo(points[0].x, points[0].y, currentSegment);
        lastMovePt = points[0];
        fillState = SkipFillState::Empty;
        break;
      case PathVerb::Line:
        if (!doConsumeDegerates || !AllPointsEqual(points, 2)) {
          AppendLine(points[1].x, points[1].y, currentSegment);
          if ((fillState == SkipFillState::Empty) && (points[0] != lastMovePt)) {
            fillState = SkipFillState::SingleLine;
            break;
          }
          fillState = SkipFillState::NonSingleLine;
        }
        break;
      case PathVerb::Quad:
        if (!doConsumeDegerates || !AllPointsEqual(points, 3)) {
          AppendQuad(points, currentSegment);
          fillState = SkipFillState::NonSingleLine;
        }
        break;
      case PathVerb::Cubic:
        if (!doConsumeDegerates || !AllPointsEqual(points, 4)) {
          AppendCubic(points[1].x, points[1].y, points[2].x, points[2].y, points[3].x, points[3].y,
                      currentSegment);
          fillState = SkipFillState::NonSingleLine;
        }
        break;
      case PathVerb::Close:
        ClosePath(currentSegment);
        currentSegment->writeToAndReset(content);
        break;
    }
  };

  path.decompose(pathIterator, nullptr);

  if (currentSegment->bytesWritten() > 0) {
    currentSegment->writeToStream(content);
  }
}

void PDFUtils::PaintPath(PathFillType fillType, const std::shared_ptr<MemoryWriteStream>& content) {
  content->writeText("f");
  if (fillType == PathFillType::EvenOdd) {
    content->writeText("*");
  }
  content->writeText("\n");
}

const char* PDFUtils::BlendModeName(BlendMode mode) {
  // PDF32000.book section 11.3.5 "Blend Mode"
  switch (mode) {
    case BlendMode::SrcOver:
    case BlendMode::Xor:          // (unsupported mode)
    case BlendMode::PlusLighter:  // (unsupported mode)
    case BlendMode::PlusDarker:   // (unsupported mode)
      return "Normal";
    case BlendMode::Screen:
      return "Screen";
    case BlendMode::Overlay:
      return "Overlay";
    case BlendMode::Darken:
      return "Darken";
    case BlendMode::Lighten:
      return "Lighten";
    case BlendMode::ColorDodge:
      return "ColorDodge";
    case BlendMode::ColorBurn:
      return "ColorBurn";
    case BlendMode::HardLight:
      return "HardLight";
    case BlendMode::SoftLight:
      return "SoftLight";
    case BlendMode::Difference:
      return "Difference";
    case BlendMode::Exclusion:
      return "Exclusion";
    case BlendMode::Multiply:
      return "Multiply";
    case BlendMode::Hue:
      return "Hue";
    case BlendMode::Saturation:
      return "Saturation";
    case BlendMode::Color:
      return "Color";
    case BlendMode::Luminosity:
      return "Luminosity";
    default:
      return nullptr;
  }
}

bool PDFUtils::InverseTransformBBox(const Matrix& matrix, Rect* boundBox) {
  Matrix inverse;
  if (!matrix.invert(&inverse)) {
    return false;
  }
  inverse.mapRect(boundBox);
  return true;
}

void PDFUtils::PopulateTilingPatternDict(PDFDictionary* pattern, const Rect& boundBox,
                                         std::unique_ptr<PDFDictionary> resources,
                                         const Matrix& matrix) {
  constexpr int Tiling_PatternType = 1;
  constexpr int ColoredTilingPattern_PaintType = 1;
  constexpr int ConstantSpacing_TilingType = 1;

  pattern->insertName("Type", "Pattern");
  pattern->insertInt("PatternType", Tiling_PatternType);
  pattern->insertInt("PaintType", ColoredTilingPattern_PaintType);
  pattern->insertInt("TilingType", ConstantSpacing_TilingType);
  pattern->insertObject("BBox", PDFUtils::RectToArray(boundBox));
  pattern->insertScalar("XStep", boundBox.width());
  pattern->insertScalar("YStep", boundBox.height());
  pattern->insertObject("Resources", std::move(resources));
  if (!matrix.isIdentity()) {
    pattern->insertObject("Matrix", PDFUtils::MatrixToArray(matrix));
  }
}

}  // namespace tgfx