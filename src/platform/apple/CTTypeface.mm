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

#include "tgfx/platform/apple/CTTypeface.h"
#include "core/utils/USE.h"

#ifndef TGFX_USE_FREETYPE
#include "core/vectors/coregraphics/CGTypeface.h"
#endif

namespace tgfx {
std::shared_ptr<Typeface> CTTypeface::MakeFromCTFont(CTFontRef ctFont) {
#ifdef TGFX_USE_FREETYPE
  USE(ctFont);
  return nullptr;
#else
  return CGTypeface::Make(static_cast<CTFontRef>(ctFont));
#endif
}

CTFontRef CTTypeface::GetCTFont(const Typeface* typeface) {
#ifdef TGFX_USE_FREETYPE
  USE(typeface);
  return nullptr;
#else
  return typeface ? static_cast<const CGTypeface*>(typeface)->getCTFont() : nullptr;
#endif
}
}  // namespace tgfx
