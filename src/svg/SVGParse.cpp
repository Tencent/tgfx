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

#include "tgfx/svg/SVGParse.h"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include "core/utils/Log.h"
#include "svg/SVGUtils.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Typeface.h"

namespace tgfx {

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

std::tuple<bool, std::shared_ptr<Path>> PathParse::FromSVGString(const char data[]) {
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

///////////////////////////////////////////////////////////////////////////////

std::string PathParse::ToSVGString(const Path& path, PathParse::PathEncoding encoding) {
  Point currentPoint = Point::Zero();
  const int relSelector = encoding == PathParse::PathEncoding::Relative ? 1 : 0;

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

    ASSERT(count > 0);
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

}  // namespace tgfx