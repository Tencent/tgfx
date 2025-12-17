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

#pragma once

#include <jni.h>
#include <memory>
#include <string>
#include "tgfx/core/ImageInfo.h"

namespace tgfx {

/**
 * Renders glyphs using Android system Canvas for color vector fonts (COLRv1/SVG).
 * This class uses pure JNI reflection to call Android graphics APIs.
 */
class GlyphRenderer {
 public:
  /**
   * Initialize JNI references. Must be called during JNI initialization.
   */
  static void JNIInit(JNIEnv* env);

  /**
   * Check if Android system rendering is available.
   */
  static bool IsAvailable();

  /**
   * Render text to pixel buffer using Android Canvas.
   * @param fontPath Path to the font file, empty for system default
   * @param text The text to render (UTF-8 encoded)
   * @param textSize Font size in pixels
   * @param width Bitmap width
   * @param height Bitmap height
   * @param offsetX X offset for drawing position
   * @param offsetY Y offset for drawing position (baseline)
   * @param dstPixels Destination pixel buffer (RGBA_8888 format)
   * @return true on success
   */
  static bool RenderGlyph(const std::string& fontPath, const std::string& text, float textSize,
                          int width, int height, float offsetX, float offsetY, void* dstPixels);

  /**
   * Measure text bounds using Android Paint.
   * @param fontPath Path to the font file, empty for system default
   * @param text The text to measure (UTF-8 encoded)
   * @param textSize Font size in pixels
   * @param bounds Output: [left, top, right, bottom]
   * @param advance Output: horizontal advance width
   * @return true on success
   */
  static bool MeasureText(const std::string& fontPath, const std::string& text, float textSize,
                          float* bounds, float* advance);

  /**
   * Get font metrics using Android Paint.
   * @param fontPath Path to the font file, empty for system default
   * @param textSize Font size in pixels
   * @param ascent Output: ascent (negative value, distance above baseline)
   * @param descent Output: descent (positive value, distance below baseline)
   * @param leading Output: leading (line spacing)
   * @return true on success
   */
  static bool GetFontMetrics(const std::string& fontPath, float textSize, float* ascent,
                             float* descent, float* leading);
};
}  // namespace tgfx
