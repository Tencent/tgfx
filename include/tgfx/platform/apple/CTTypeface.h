/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include <CoreText/CoreText.h>
#include "tgfx/core/Typeface.h"

namespace tgfx {

/**
 * NativeTypeface provides convenience functions to create typefaces from platform-specific
 * CTFontRef handles and access the CTFontRef handle in a Typeface instance.
 */
class CTTypeface {
 public:
  /**
   * Creates a typeface for the specified CTFontRef. Returns nullptr if the ctFont is nil.
   */
  static std::shared_ptr<Typeface> MakeFromCTFont(CTFontRef ctFont);

  /**
   * Returns the platform-specific CTFontRef handle for a given Typeface. Note that the returned
   * CTFontRef gets released when the source Typeface is destroyed.
   */
  static CTFontRef GetCTFont(const Typeface* typeface);
};
}  // namespace tgfx
