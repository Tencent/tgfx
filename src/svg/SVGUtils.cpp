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

#include "SVGUtils.h"
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include "core/codecs/jpeg/JpegCodec.h"
#include "core/codecs/png/PngCodec.h"
#include "core/utils/Log.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/ImageCodec.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Pixmap.h"

namespace tgfx {

std::string ToSVGTransform(const Matrix& matrix) {
  DEBUG_ASSERT(!matrix.isIdentity());

  std::stringstream strStream;
  // http://www.w3.org/TR/SVG/coords.html#TransformMatrixDefined
  //    | a c e |
  //    | b d f |
  //    | 0 0 1 |
  //matrix(scaleX skewY skewX scaleY transX transY)
  strStream << "matrix(" << matrix.getScaleX() << " " << matrix.getSkewY() << " "
            << matrix.getSkewX() << " " << matrix.getScaleY() << " " << matrix.getTranslateX()
            << " " << matrix.getTranslateY() << ")";
  return strStream.str();
}

// For maximum compatibility, do not convert colors to named colors, convert them to hex strings.
std::string ToSVGColor(Color color) {
  auto r = static_cast<uint8_t>(color.red * 255);
  auto g = static_cast<uint8_t>(color.green * 255);
  auto b = static_cast<uint8_t>(color.blue * 255);

  // Some users care about every byte here, so we'll use hex colors with single-digit channels
  // when possible.
  uint8_t rh = r >> 4;
  uint8_t rl = r & 0xf;
  uint8_t gh = g >> 4;
  uint8_t gl = g & 0xf;
  uint8_t bh = b >> 4;
  uint8_t bl = b & 0xf;
  if ((rh == rl) && (gh == gl) && (bh == bl)) {
    char buffer[8];
    snprintf(buffer, sizeof(buffer), "#%1X%1X%1X", rh, gh, bh);
    return std::string(buffer);
  }
  char buffer[8];
  snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", r, g, b);
  return std::string(buffer);
}

std::string ToSVGCap(LineCap cap) {
  static const std::array<std::string, 3> capMap = {
      "",       // Butt_Cap (default)
      "round",  // Round_Cap
      "square"  // Square_Cap
  };

  auto index = static_cast<size_t>(cap);
  DEBUG_ASSERT(index < capMap.size());
  return capMap[index];
}

std::string ToSVGJoin(LineJoin join) {
  static const std::array<std::string, 3> joinMap = {
      "",       // Miter_Join (default)
      "round",  // Round_Join
      "bevel"   // Bevel_Join
  };

  auto index = static_cast<size_t>(join);
  DEBUG_ASSERT(index < joinMap.size());
  return joinMap[index];
}

std::string ToSVGBlendMode(BlendMode mode) {
  // Not all blend modes have corresponding SVG properties. Use an empty string for those,
  // which will later be converted to "normal".
  constexpr size_t blendModeCount = static_cast<size_t>(BlendMode::PlusDarker) + 1;
  static const std::array<std::string, blendModeCount> blendModeMap = {
      "",              // Clear
      "",              // Src
      "",              // Dst
      "normal",        // SrcOver
      "",              // DstOver
      "",              // SrcIn
      "",              // DstIn
      "",              // SrcOut
      "",              // DstOut
      "",              // SrcATop
      "",              // DstATop
      "",              // Xor
      "plus-lighter",  // PlusLighter
      "",              // Modulate
      "screen",        //Screen
      "overlay",       // Overlay
      "darken",        // Darken
      "lighten",       // Lighten
      "color-dodge",   // ColorDodge
      "color-burn",    // ColorBurn
      "hard-light",    // HardLight
      "soft-light",    // SoftLight
      "difference",    // Difference
      "exclusion",     // Exclusion
      "multiply",      // Multiply
      "hue",           // Hue
      "saturation",    // Saturation
      "color",         // Color
      "luminosity",    // Luminosity
      "plus-darker"    // PlusDarker
  };
  auto index = static_cast<size_t>(mode);
  DEBUG_ASSERT(index < blendModeCount);
  const auto& blendStr = blendModeMap[index];
  if (blendStr.empty()) {
    return "";
  }
  return "mix-blend-mode:" + blendStr;
}

std::string ToSVGPath(const Path& path, PathEncoding encoding) {
  Point currentPoint = Point::Zero();
  const int relSelector = encoding == PathEncoding::Relative ? 1 : 0;

  const auto appendCommand = [&](std::string& inputString, char commandChar, const Point points[],
                                 size_t offset, size_t count) {
    // Use lower case cmds for relative encoding.
    commandChar = static_cast<char>(commandChar + 32 * relSelector);
    inputString.push_back(commandChar);
    for (size_t i = 0; i < count; ++i) {
      const auto pt = points[offset + i] - currentPoint;
      if (i > 0) {
        inputString.push_back(' ');
      }
      inputString.append(FloatToString(pt.x) + " " + FloatToString(pt.y));
    }

    DEBUG_ASSERT(count > 0);
    // For relative encoding, track the current point (otherwise == origin).
    currentPoint = {points[offset + count - 1].x * static_cast<float>(relSelector),
                    points[offset + count - 1].y * static_cast<float>(relSelector)};
  };

  auto pathIter = [&](PathVerb verb, const Point points[4], void* info) -> void {
    auto* castedString = reinterpret_cast<std::string*>(info);
    switch (verb) {
      case PathVerb::Move:
        appendCommand(*castedString, 'M', points, 0, 1);
        break;
      case PathVerb::Line:
        appendCommand(*castedString, 'L', points, 1, 1);
        break;
      case PathVerb::Quad:
        appendCommand(*castedString, 'Q', points, 1, 2);
        break;
      case PathVerb::Cubic:
        appendCommand(*castedString, 'C', points, 1, 3);
        break;
      case PathVerb::Close:
        castedString->push_back('Z');
        break;
    }
  };

  std::string svgString;
  path.decompose(pathIter, &svgString);
  return svgString;
}

std::string FloatToString(float value) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(4) << value;
  std::string result = out.str();
  result.erase(result.find_last_not_of('0') + 1, std::string::npos);  // Remove trailing zeros
  if (result.back() == '.') {
    result.pop_back();
  }
  return result;
}

std::shared_ptr<Data> Base64Decode(const std::string& encodedString) {
  static const std::array<unsigned char, 128> decodingTable = {
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62,
      64, 64, 64, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64, 64, 0,
      1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
      23, 24, 25, 64, 64, 64, 64, 64, 64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38,
      39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64};

  size_t inLength = encodedString.size();
  if (inLength % 4 != 0) {
    return nullptr;
  }

  size_t outLength = inLength / 4 * 3;
  if (encodedString[inLength - 1] == '=') {
    outLength--;
  }
  if (encodedString[inLength - 2] == '=') {
    outLength--;
  }

  auto* out = static_cast<unsigned char*>(malloc(outLength));
  auto outData = Data::MakeAdopted(out, outLength, Data::FreeProc);

  for (size_t i = 0, j = 0; i < inLength;) {
    uint32_t a = encodedString[i] == '='
                     ? 0 & i++
                     : decodingTable[static_cast<unsigned char>(encodedString[i++])];
    uint32_t b = encodedString[i] == '='
                     ? 0 & i++
                     : decodingTable[static_cast<unsigned char>(encodedString[i++])];
    uint32_t c = encodedString[i] == '='
                     ? 0 & i++
                     : decodingTable[static_cast<unsigned char>(encodedString[i++])];
    uint32_t d = encodedString[i] == '='
                     ? 0 & i++
                     : decodingTable[static_cast<unsigned char>(encodedString[i++])];

    uint32_t triple = (a << 18) + (b << 12) + (c << 6) + d;

    if (j < outLength) {
      out[j++] = (triple >> 16) & 0xFF;
    }
    if (j < outLength) {
      out[j++] = (triple >> 8) & 0xFF;
    }
    if (j < outLength) {
      out[j++] = triple & 0xFF;
    }
  }

  return outData;
}

void Base64Encode(unsigned char const* bytesToEncode, size_t length, char* ret) {
  static const std::string base64_chars =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz"
      "0123456789+/";
  int i = 0;
  int j = 0;
  unsigned char char_array_3[3];
  unsigned char char_array_4[4];

  while (length--) {
    char_array_3[i++] = *(bytesToEncode++);
    if (i == 3) {
      char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
      char_array_4[1] = static_cast<unsigned char>(((char_array_3[0] & 0x03) << 4) +
                                                   ((char_array_3[1] & 0xf0) >> 4));
      char_array_4[2] = static_cast<unsigned char>(((char_array_3[1] & 0x0f) << 2) +
                                                   ((char_array_3[2] & 0xc0) >> 6));
      char_array_4[3] = char_array_3[2] & 0x3f;

      for (i = 0; (i < 4); i++) {
        *ret++ = base64_chars[char_array_4[i]];
      }
      i = 0;
    }
  }

  if (i) {
    for (j = i; j < 3; j++) {
      char_array_3[j] = '\0';
    }

    char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
    char_array_4[1] = static_cast<unsigned char>(((char_array_3[0] & 0x03) << 4) +
                                                 ((char_array_3[1] & 0xf0) >> 4));
    char_array_4[2] = static_cast<unsigned char>(((char_array_3[1] & 0x0f) << 2) +
                                                 ((char_array_3[2] & 0xc0) >> 6));
    char_array_4[3] = char_array_3[2] & 0x3f;

    for (j = 0; (j < i + 1); j++) {
      *ret++ = base64_chars[char_array_4[j]];
    }

    while ((i++ < 3)) {
      *ret++ = '=';
    }
  }
}

// Returns data uri from bytes.
// it will use any cached data if available, otherwise will
// encode as png.
std::shared_ptr<Data> AsDataUri(const Pixmap& pixmap) {
  if (pixmap.isEmpty()) {
    return nullptr;
  }

  auto imageData = ImageCodec::Encode(pixmap, EncodedFormat::PNG, 100);
  if (!imageData) {
    return nullptr;
  }
  return AsDataUri(imageData);
}

std::shared_ptr<Data> AsDataUri(const std::shared_ptr<Data>& encodedData) {
  static constexpr auto pngPrefix = "data:image/png;base64,";
  static constexpr auto jpgPrefix = "data:image/jpeg;base64,";

  const char* prefix = nullptr;
  if (PngCodec::IsPng(encodedData)) {
    prefix = pngPrefix;
  } else if (JpegCodec::IsJpeg(encodedData)) {
    prefix = jpgPrefix;
  } else {
    return nullptr;
  }
  size_t prefixLength = strlen(prefix);
  size_t base64Size = ((encodedData->size() + 2) / 3) * 4 + 1;  // +1 for char* terminator
  auto bufferSize = prefixLength + base64Size;
  auto* dest = static_cast<char*>(malloc(bufferSize));
  memcpy(dest, prefix, prefixLength);
  Base64Encode(encodedData->bytes(), encodedData->size(), dest + prefixLength);
  dest[bufferSize - 1] = '\0';

  auto dataUri = Data::MakeAdopted(dest, bufferSize, Data::FreeProc);
  return dataUri;
}

namespace {
inline bool is_between(int c, int min, int max) {
  return static_cast<unsigned>(c - min) <= static_cast<unsigned>(max - min);
}

inline bool is_ws(int c) {
  return is_between(c, 1, 32);
}

inline bool is_digit(int c) {
  return is_between(c, '0', '9');
}

int to_hex(int c) {
  if (is_digit(c)) {
    return c - '0';
  }

  c |= 0x20;  // make us lower-case
  return is_between(c, 'a', 'f') ? c + 10 - 'a' : -1;
}

inline bool is_hex(int c) {
  return to_hex(c) >= 0;
}

const char* skip_ws(const char str[]) {
  ASSERT(str);
  while (is_ws(*str)) {
    str++;
  }
  return str;
}

template <typename T>
tgfx::Unichar next_fail(const T** ptr, const T* end) {
  *ptr = end;
  return -1;
}

inline bool is_sep(int c) {
  return is_ws(c) || c == ',' || c == ';';
}

const char* skip_sep(const char str[]) {
  ASSERT(str);
  while (is_sep(*str)) {
    str++;
  }
  return str;
}

bool lookup_str(const char str[], const char** table, int count) {
  while (--count >= 0) {
    if (!strcmp(str, table[count])) {
      return true;
    }
  }
  return false;
}

inline bool is_lower(int c) {
  return is_between(c, 'a', 'z');
}

inline int to_upper(int c) {
  return c - 'a' + 'A';
}

// If unable to read count points from str into value, this will return nullptr
// to signal the failure. Otherwise, it will return the next offset to read from.
const char* find_points(const char str[], Point value[], int count, bool isRelative,
                        Point* relative) {
  str = SVGParse::FindScalars(str, &value[0].x, count * 2);
  if (isRelative) {
    for (int index = 0; index < count; index++) {
      value[index].x += relative->x;
      value[index].y += relative->y;
    }
  }
  return str;
}

// If unable to read a scalar from str into value, this will return nullptr
// to signal the failure. Otherwise, it will return the next offset to read from.
const char* find_scalar(const char str[], float* value, bool isRelative, float relative) {
  str = SVGParse::FindScalar(str, value);
  if (!str) {
    return nullptr;
  }
  if (isRelative) {
    *value += relative;
  }
  str = skip_sep(str);
  return str;
}

// https://www.w3.org/TR/SVG11/paths.html#PathDataBNF
// flag:
//    "0" | "1"
const char* find_flag(const char str[], bool* value) {
  if (!str) {
    return nullptr;
  }
  if (str[0] != '1' && str[0] != '0') {
    return nullptr;
  }
  *value = str[0] != '0';
  str = skip_sep(str + 1);
  return str;
}
}  // namespace

std::tuple<bool, std::shared_ptr<Path>> PathMakeFromSVGString(const std::string& pathString) {
  const char* data = pathString.c_str();
  // We will write all data to this local path and only write it
  // to result if the whole parsing succeeds.
  auto path = std::make_shared<Path>();
  auto first = Point::Zero();
  auto opOrigin = Point::Zero();
  auto lastOpOrigin = Point::Zero();

  // We will use find_points and find_scalar to read into these.
  // There might not be enough data to fill them, so to avoid
  // MSAN warnings about using uninitialized bytes, we initialize
  // them there.
  Point points[3] = {};
  float scratch = 0;
  char op = '\0';
  char previousOp = '\0';
  bool relative = false;
  for (;;) {
    if (!data) {
      // Truncated data
      return {false, nullptr};
    }
    data = skip_ws(data);
    if (data[0] == '\0') {
      break;
    }
    char ch = data[0];
    if (is_digit(ch) || ch == '-' || ch == '+' || ch == '.') {
      if (op == '\0' || op == 'Z') {
        return {false, nullptr};
      }
    } else if (is_sep(ch)) {
      data = skip_sep(data);
    } else {
      op = ch;
      relative = false;
      if (is_lower(op)) {
        op = static_cast<char>(to_upper(op));
        relative = true;
      }
      data++;
      data = skip_sep(data);
    }
    switch (op) {
      case 'M':  // Move
        data = find_points(data, points, 1, relative, &opOrigin);
        // find_points might have failed, so this might be the
        // previous point. However, data will be set to nullptr
        // if it failed, so we will check this at the top of the loop.
        path->moveTo(points[0]);
        previousOp = '\0';
        op = 'L';
        opOrigin = points[0];
        break;
      case 'L':  // Line
        data = find_points(data, points, 1, relative, &opOrigin);
        path->lineTo(points[0]);
        opOrigin = points[0];
        break;
      case 'H':  // Horizontal Line
        data = find_scalar(data, &scratch, relative, opOrigin.x);
        // Similarly, if there wasn't a scalar to read, data will
        // be set to nullptr and this lineTo is bogus but will
        // be ultimately ignored when the next time through the loop
        // detects that and bails out.
        path->lineTo(scratch, opOrigin.y);
        opOrigin.x = scratch;
        break;
      case 'V':  // Vertical Line
        data = find_scalar(data, &scratch, relative, opOrigin.y);
        path->lineTo(opOrigin.x, scratch);
        opOrigin.y = scratch;
        break;
      case 'C':  // Cubic Bezier Curve
        data = find_points(data, points, 3, relative, &opOrigin);
        goto cubicCommon;
      case 'S':  // Continued "Smooth" Cubic Bezier Curve
        data = find_points(data, &points[1], 2, relative, &opOrigin);
        points[0] = opOrigin;
        if (previousOp == 'C' || previousOp == 'S') {
          points[0].x -= lastOpOrigin.x - opOrigin.x;
          points[0].y -= lastOpOrigin.y - opOrigin.y;
        }
      cubicCommon:
        path->cubicTo(points[0], points[1], points[2]);
        lastOpOrigin = points[1];
        opOrigin = points[2];
        break;
      case 'Q':  // Quadratic Bezier Curve
        data = find_points(data, points, 2, relative, &opOrigin);
        goto quadraticCommon;
      case 'T':  // Continued Quadratic Bezier Curve
        data = find_points(data, &points[1], 1, relative, &opOrigin);
        points[0] = opOrigin;
        if (previousOp == 'Q' || previousOp == 'T') {
          points[0].x -= lastOpOrigin.x - opOrigin.x;
          points[0].y -= lastOpOrigin.y - opOrigin.y;
        }
      quadraticCommon:
        path->quadTo(points[0], points[1]);
        lastOpOrigin = points[0];
        opOrigin = points[1];
        break;
      case 'A': {  // Arc (Elliptical)
        auto xyRadii = Point::Zero();
        float angle = 0.0f;
        bool largeArc = true;
        bool sweep = true;

        if (data = find_points(data, &xyRadii, 1, false, nullptr); !data) break;
        if (data = skip_sep(data); !data) break;
        if (data = find_scalar(data, &angle, false, 0); !data) break;
        if (data = skip_sep(data); !data) break;
        if (data = find_flag(data, &largeArc); !data) break;
        if (data = skip_sep(data); !data) break;
        if (data = find_flag(data, &sweep); !data) break;
        if (data = skip_sep(data); !data) break;
        if (data = find_points(data, &points[0], 1, relative, &opOrigin); !data) break;

        auto arcSize = largeArc ? PathArcSize::Large : PathArcSize::Small;
        path->arcTo(xyRadii.x, xyRadii.y, angle, arcSize, !sweep, points[0]);
        path->getLastPoint(&opOrigin);
      } break;
      case 'Z':  // Close Path
        path->close();
        opOrigin = first;
        break;
      default:
        return {false, nullptr};
    }
    if (previousOp == 0) {
      first = opOrigin;
    }
    previousOp = op;
  }
  return {true, path};
}

const char* SVGParse::FindHex(const char str[], uint32_t* value) {
  ASSERT(str);
  str = skip_ws(str);

  if (!is_hex(*str)) {
    return nullptr;
  }

  uint32_t n = 0;
  int max_digits = 8;
  int digit;
  while ((digit = to_hex(*str)) >= 0) {
    if (--max_digits < 0) {
      return nullptr;
    }
    n = (n << 4) | static_cast<uint32_t>(digit);
    str += 1;
  }

  if (*str == 0 || is_ws(*str)) {
    if (value) {
      *value = n;
    }
    return str;
  }
  return nullptr;
}

const char* SVGParse::FindS32(const char str[], int32_t* value) {
  ASSERT(str);
  str = skip_ws(str);

  int sign = 1;
  int64_t maxAbsValue = std::numeric_limits<int>::max();
  if (*str == '-') {
    sign = -1;
    maxAbsValue = -static_cast<int64_t>(std::numeric_limits<int>::min());
    str += 1;
  }

  if (!is_digit(*str)) {
    return nullptr;
  }

  int64_t n = 0;
  while (is_digit(*str)) {
    n = 10 * n + *str - '0';
    if (n > maxAbsValue) {
      return nullptr;
    }

    str += 1;
  }

  if (value) {
    *value = static_cast<int32_t>(sign * n);
  }
  return str;
}

const char* SVGParse::FindScalar(const char str[], float* value) {
  ASSERT(str);
  str = skip_ws(str);

  char* stop;
  auto v = static_cast<float>(strtod(str, &stop));
  if (str == stop) {
    return nullptr;
  }
  if (value) {
    *value = v;
  }
  return stop;
}

const char* SVGParse::FindScalars(const char str[], float value[], int count) {
  ASSERT(count >= 0);

  if (count > 0) {
    for (;;) {
      str = SVGParse::FindScalar(str, value);
      if (--count == 0 || str == nullptr) {
        break;
      }

      // keep going
      str = skip_sep(str);
      if (value) {
        value += 1;
      }
    }
  }
  return str;
}

bool SVGParse::FindBool(const char str[], bool* value) {
  static const char* yesSet[] = {"yes", "1", "true"};
  static const char* noSet[] = {"no", "0", "false"};

  if (lookup_str(str, yesSet, std::size(yesSet))) {
    if (value) {
      *value = true;
    }
    return true;
  } else if (lookup_str(str, noSet, std::size(noSet))) {
    if (value) {
      *value = false;
    }
    return true;
  }
  return false;
}

int SVGParse::FindList(const char target[], const char list[]) {
  auto len = strlen(target);
  int index = 0;

  for (;;) {
    const char* end = strchr(list, ',');
    size_t entryLen = end == nullptr ? strlen(list) : static_cast<size_t>(end - list);

    if (entryLen == len && memcmp(target, list, len) == 0) {
      return index;
    }
    if (end == nullptr) {
      break;
    }

    list = end + 1;  // skip the ','
    index += 1;
  }
  return -1;
}

}  // namespace tgfx