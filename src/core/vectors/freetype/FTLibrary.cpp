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

#include "FTLibrary.h"
#include FT_MODULE_H

namespace tgfx {
FT_Library FTLibrary::Get() {
  static FTLibrary& ftLibrary = *new FTLibrary();
  return ftLibrary.library;
}
FTLibrary::FTLibrary() {
  FT_Init_FreeType(&library);
}

FTLibrary::~FTLibrary() {
  if (library) {
    FT_Done_Library(library);
  }
}
}  // namespace tgfx
