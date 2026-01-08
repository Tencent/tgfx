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

#include "WGLProcGetter.h"
#include "core/utils/Log.h"

namespace tgfx {
WGLProcGetter::WGLProcGetter() {
  fLibrary = LoadLibraryA("opengl32.dll");
}

WGLProcGetter::~WGLProcGetter() {
  if (fLibrary) {
    FreeLibrary(fLibrary);
  }
}

void* WGLProcGetter::getProcAddress(const char name[]) const {
  DEBUG_ASSERT(wglGetCurrentContext());
  if (auto address = GetProcAddress(fLibrary, name)) {
    return address;
  }
  if (auto address = wglGetProcAddress(name)) {
    return address;
  }
  return nullptr;
}

std::unique_ptr<GLProcGetter> GLProcGetter::Make() {
  return std::make_unique<WGLProcGetter>();
}
}  // namespace tgfx
