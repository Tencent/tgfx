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

#include "tgfx/svg/SVGPathParser.h"
#include "core/NoConicsPathIterator.h"
#include "core/utils/Log.h"
#include "svg/SVGUtils.h"
#include "tgfx/core/Path.h"

namespace tgfx {

namespace {
inline bool is_lower(int c) {
  return c >= 'a' && c <= 'z';
}

inline int to_upper(int c) {
  return c - 'a' + 'A';
}

inline bool is_ws(int c) {
  return c >= 1 && c <= 32;
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

inline bool is_digit(int c) {
  return c >= '0' && c <= '9';
}

const char* skip_ws(const char str[]) {
  ASSERT(str);
  while (is_ws(*str)) {
    str++;
  }
  return str;
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

std::string SVGPathParser::ToSVGString(const Path& path, PathEncoding encoding) {
  Point currentPoint = {};
  const int relSelector = encoding == PathEncoding::Relative ? 1 : 0;

  const auto appendCommand = [&](std::string& inputString, char commandChar, const Point points[],
                                 size_t offset, size_t count) {
    // Use lower case cmds for relative encoding.
    commandChar = static_cast<char>(commandChar + (32 * relSelector));
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

  std::string svgString;
  NoConicsPathIterator iterator(path);
  for (auto segment : iterator) {
    switch (segment.verb) {
      case PathVerb::Move:
        appendCommand(svgString, 'M', segment.points, 0, 1);
        break;
      case PathVerb::Line:
        appendCommand(svgString, 'L', segment.points, 1, 1);
        break;
      case PathVerb::Quad:
        appendCommand(svgString, 'Q', segment.points, 1, 2);
        break;
      case PathVerb::Cubic:
        appendCommand(svgString, 'C', segment.points, 1, 3);
        break;
      case PathVerb::Close:
        svgString.push_back('Z');
        break;
      default:
        break;
    }
  }
  return svgString;
}

std::shared_ptr<Path> SVGPathParser::FromSVGString(const std::string& pathString) {
  const char* data = pathString.c_str();
  // We will write all data to this local path and only write it to result if the whole parsing
  // succeeds.
  auto path = std::make_shared<Path>();
  Point first = {};
  Point opOrigin = {};
  Point lastOpOrigin = {};

  // We will use find_points and find_scalar to read into these. There might not be enough data to
  // fill them, so to avoid MSAN warnings about using uninitialized bytes, we initialize them there.
  Point points[3] = {};
  float scratch = 0;
  char op = '\0';
  char previousOp = '\0';
  bool relative = false;
  for (;;) {
    if (!data) {
      // Truncated data
      return nullptr;
    }
    data = skip_ws(data);
    if (data[0] == '\0') {
      break;
    }
    char ch = data[0];
    if (is_digit(ch) || ch == '-' || ch == '+' || ch == '.') {
      if (op == '\0' || op == 'Z') {
        return nullptr;
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
        // find_points might have failed, so this might be the previous point. However, data will be
        // set to nullptr if it failed, so we will check this at the top of the loop.
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
        // Similarly, if there wasn't a scalar to read, data will be set to nullptr and this lineTo
        // is bogus but will be ultimately ignored when the next time through the loop  detects that
        // and bails out.
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
        Point xyRadii = {};
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
        return nullptr;
    }
    if (previousOp == 0) {
      first = opOrigin;
    }
    previousOp = op;
  }
  return path;
}

}  // namespace tgfx
