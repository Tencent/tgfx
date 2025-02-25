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

#include <windows.h>
#include <string>

DECLARE_HANDLE(HPBUFFER);

namespace tgfx {
class WGLExtensions {
 public:
  WGLExtensions();

  /**
 * Determines if an extension is available for a given DC.
 * it is necessary to check this before calling other class functions.
  */
  bool hasExtension(HDC dc, const char* ext) const;

  BOOL choosePixelFormat(HDC hdc, const int*, const FLOAT*, UINT, int*, UINT*) const;

  BOOL swapInterval(int interval) const;

  HPBUFFER createPbuffer(HDC,int,int,int,const int*) const;

  HDC getPbufferDC(HPBUFFER) const;

  int releasePbufferDC(HPBUFFER, HDC) const;

  BOOL destroyPbuffer(HPBUFFER) const;
};
}  // namespace tgfx
