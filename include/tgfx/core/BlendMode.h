/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

namespace tgfx {
/**
 * Defines constant values for visual blend mode effects.
 */
enum class BlendMode {
  /**
   * Replaces destination with zero: fully transparent.
   */
  Clear,
  /**
   * Replaces destination.
   */
  Src,
  /**
   * Preserves destination.
   */
  Dst,
  /**
   * Source over destination.
   */
  SrcOver,
  /**
   * Destination over source.
   */
  DstOver,
  /**
   * Source trimmed inside destination.
   */
  SrcIn,
  /**
   * Destination trimmed by source.
   */
  DstIn,
  /**
   * Source trimmed outside destination.
   */
  SrcOut,
  /**
   * Destination trimmed outside source.
   */
  DstOut,
  /**
   * Source inside destination blended with destination.
   */
  SrcATop,
  /**
   * Destination inside source blended with source.
   */
  DstATop,
  /**
   * Each of source and destination trimmed outside the other.
   */
  Xor,
  /**
   * Adds the colors together. If the result exceeds 1 (for RGB), it displays white.
   */
  PlusLighter,
  /**
   * Product of premultiplied colors; darkens destination.
   */
  Modulate,
  /**
   * Multiply inverse of pixels, inverting result; brightens destination.
   */
  Screen,
  /**
   * Multiply or screen, depending on destination.
   */
  Overlay,
  /**
   * Darker of source and destination.
   */
  Darken,
  /**
   * Lighter of source and destination.
   */
  Lighten,
  /**
   * Brighten destination to reflect source.
   */
  ColorDodge,
  /**
   * Darken destination to reflect source.
   */
  ColorBurn,
  /**
   * Multiply or screen, depending on source.
   */
  HardLight,
  /**
   * Lighten or darken, depending on source.
   */
  SoftLight,
  /**
   * Subtract darker from lighter with higher contrast.
   */
  Difference,
  /**
   * Subtract darker from lighter with lower contrast.
   */
  Exclusion,
  /**
   * Multiply source with destination, darkening image.
   */
  Multiply,
  /**
   * Hue of source with saturation and luminosity of destination.
   */
  Hue,
  /**
   * Saturation of source with hue and luminosity of destination.
   */
  Saturation,
  /**
   * Hue and saturation of source with luminosity of destination.
   */
  Color,
  /**
   * Luminosity of source with hue and saturation of destination.
   */
  Luminosity,
  /**
   * A variation of PlusLighter. It adds the colors together but subtracts 1 from the final values,
   * making any values below 0 black.
   */
  PlusDarker,
};
}  // namespace tgfx
