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
      ret += base64_chars[char_array_4[j]];
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
  static constexpr auto pngPrefix = "data:image/png;base64,";
  size_t prefixLength = strlen(pngPrefix);

  auto imageData = ImageCodec::Encode(pixmap, EncodedFormat::PNG, 100);
  if (!imageData) {
    return nullptr;
  }

  size_t base64Size = ((imageData->size() + 2) / 3) * 4;
  auto bufferSize = prefixLength + base64Size;
  auto* dest = static_cast<char*>(malloc(bufferSize));
  memcpy(dest, pngPrefix, prefixLength);
  Base64Encode(imageData->bytes(), imageData->size(), dest + prefixLength);
  dest[bufferSize - 1] = '\0';

  auto dataUri = Data::MakeAdopted(dest, bufferSize, Data::FreeProc);
  return dataUri;
}

}  // namespace tgfx