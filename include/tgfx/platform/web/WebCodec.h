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
 * WebCodec provides functions to enable or disable async decoding support for native codecs using
 * web platform APIs. This setting does not affect embedded third-party codecs.
 */
class WebCodec {
 public:
  /**
   * Returns true if async decoding support for web native codecs web images is enabled. When
   * enabled, ImageBuffers generated from web native codecs won't be fully decoded immediately.
   * Instead, they will trigger promise-awaiting calls before generating textures, speeding up the
   * process of decoding multiple images simultaneously. Avoid enabling this if your rendering
   * process requires multiple flush() calls to the screen in a single frame, as it may cause screen
   * tearing, where parts of the screen update while others don’t. The default value is false.
   */
  static bool AllowsAsyncDecoding();

  /**
   * Sets whether async decoding support for web native codecs is enabled.
   */
  static void SetAllowsAsyncDecoding(bool enabled);
};
}  // namespace tgfx
