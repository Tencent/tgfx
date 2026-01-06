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

#include "tgfx/core/Typeface.h"

namespace tgfx {

/**
 * A 4-byte tag used to identify a font table.
 * The tag is stored in big-endian order, so the first byte is the most significant.
 */
static constexpr FontTableTag SetFourByteTag(char a, char b, char c, char d) {
  return ((static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) |
          (static_cast<uint32_t>(c) << 8) | static_cast<uint32_t>(d));
}

}  // namespace tgfx
