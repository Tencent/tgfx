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

#pragma once

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

/**
 * Two ways to describe paths in SVG
 */
enum class PathEncoding {
  /**
   * Each step's point is an absolute coordinate, and the step letter is uppercase
   */
  Absolute,
  /**
   * Each step's point is a relative coordinate to the previous point, and the step letter is
   *lowercase
   */
  Relative,
};

std::string ToSVGPath(const Path& path, PathEncoding = PathEncoding::Absolute);

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

/**
 * Draws a image onto a surface and reads the pixels from the surface.
 */
Bitmap ImageToBitmap(Context* gpuContext, const std::shared_ptr<Image>& image);

/**
 * Returns data uri from bytes.
 * it will use any cached data if available, otherwise will encode as png.
 */
std::shared_ptr<Data> AsDataUri(const Bitmap& bitmap);

}  // namespace tgfx