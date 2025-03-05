/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

namespace tgfx {
DECLARE_HANDLE(HPBUFFER);

/**
 * This function initializes a device context (DC) and creates an OpenGL context
 * associated with the provided window handle (HWND). If successful, the device context
 * and OpenGL context are returned through the output parameters.
 */
bool CreateWGLContext(HWND nativeWindow, HGLRC sharedContext, HDC& deviceContext, HGLRC& glContext);

/**
 * This function creates a pbuffer, initializes a device context (DC) and creates an OpenGL context
 * associated with the pbuffer (HPBUFFER). If successful, the pbuffer, device context
 * and OpenGL context are returned through the output parameters.
 */
bool CreatePbufferContext(HGLRC sharedContext, HPBUFFER& pBuffer, HDC& deviceContext,
                          HGLRC& glContext);

bool ReleasePbufferDC(HPBUFFER pBuffer, HDC deviceContext);

bool DestroyPbuffer(HPBUFFER pBuffer);

}  // namespace tgfx
