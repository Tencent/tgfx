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

#pragma once

#include <memory>
#include <string>
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Pixmap.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/core/Surface.h"
#include "tgfx/core/Typeface.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {

std::string ToSVGTransform(const Matrix& matrix);

/**
 * For maximum compatibility, do not convert colors to named colors, convert them to hex strings.
 */
std::string ToSVGColor(Color color);

std::string ToSVGCap(LineCap cap);

std::string ToSVGJoin(LineJoin join);

std::string ToSVGBlendMode(BlendMode mode);

/**
 * Retain 4 decimal places and remove trailing zeros.
 */
std::string FloatToString(float value);

void Base64Encode(unsigned char const* bytesToEncode, size_t length, char* ret);

std::shared_ptr<Data> Base64Decode(const std::string& encodedString);

/**
 * Returns data uri from bytes.
 * it will use any cached data if available, otherwise will encode as png.
 */
std::shared_ptr<Data> AsDataUri(const Pixmap& pixmap);

std::shared_ptr<Data> AsDataUri(const std::shared_ptr<Data>& encodedData);

std::shared_ptr<Image> ConvertImageColorSpace(const std::shared_ptr<Image>& image, Context* context,
                                              const std::shared_ptr<ColorSpace>& targetColorSpace,
                                              const std::shared_ptr<ColorSpace>& assignColorSpace);

inline Color Uint32ToColor(uint32_t value) {
  return Color::FromRGBA((value >> 16) & 0xff, (value >> 8) & 0xff, (value >> 0) & 0xff,
                         (value >> 24) & 0xff);
}

// Common functions for SVG conversion, used to find specific data types in a string.
// Returns the char pointer after the found data, or nullptr if not found.
class SVGParse {
 public:
  static const char* FindHex(const char str[], uint32_t* value);
  static const char* FindNamedColor(const char str[], Color* color);
  static const char* FindS32(const char str[], int32_t* value);
  static const char* FindScalar(const char str[], float* value);
  static const char* FindScalars(const char str[], float value[], int count);
  static bool FindBool(const char str[], bool* value);
  // return the index of str in list[], or -1 if not found
  static int FindList(const char target[], const char list[]);
};

}  // namespace tgfx