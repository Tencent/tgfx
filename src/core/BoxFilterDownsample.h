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
#include "tgfx/core/ImageInfo.h"

namespace tgfx {

struct PixelLayout {
  int width;
  int height;
  int rowBytes;
};

/**
 * Performs box filter-based downsampling on an image with support for both single-channel and
 * 4-channel RGBA data.
 *
 * This function implements an area-averaging algorithm that works by dividing the source image into
 * rectangular regions and computing the average pixel value for each region to produce the output
 * pixel. It automatically selects between two optimized implementations based on whether the
 * scaling ratio is an exact integer.
 *
 * @param inputPixels Pointer to the source image pixel data
 * @param inputLayout  Structure describing source image dimensions and layout
 * @param outputPixels Pointer to the destination buffer where downsampled image will be stored
 * @param outputLayout Structure describing destination image dimensions and layout
 * @param isOneComponent Indicates whether each pixel has a single channel (true) or four channels.
 * channels (false)
 *
 */
void BoxFilterDownsample(const void* inputPixels, const PixelLayout& inputLayout,
                         void* outputPixels, const PixelLayout& outputLayout, bool isOneComponent);
}  // namespace tgfx
