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

#include "ft2build.h"
#include FT_FREETYPE_H

namespace tgfx {
// The thread safety model of FreeType https://github.com/behdad/ftthread:
// 1. A FT_Face object can only be safely used from one thread at a time.
// 2. A FT_Library object can be used without modification from multiple threads at the same time.
// 3. FT_Face creation / destruction with the same FT_Library object can only be done from one
//    thread at a time.
class FTLibrary {
 public:
  static FT_Library Get();

  FTLibrary();

  FTLibrary(const FTLibrary&) = delete;

  FTLibrary& operator=(const FTLibrary&) = delete;

  ~FTLibrary();

 private:
  FT_Library library = nullptr;
};
}  // namespace tgfx
