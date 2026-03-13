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

// clang-format off
// NOMINMAX must be defined before windows.h to prevent min/max macro pollution.
#define NOMINMAX
// windows.h must precede GL/gl.h because GL/gl.h depends on WINGDIAPI and APIENTRY macros.
#include <windows.h>
#include <GL/gl.h>
// clang-format on
#include <vector>
#include "tgfx/platform/HardwareBuffer.h"

struct ID3D11Device;

// WGL_NV_DX_interop function typedefs
typedef HANDLE(WINAPI* PFNWGLDXOPENDEVICENVPROC)(ID3D11Device* dxDevice);
typedef BOOL(WINAPI* PFNWGLDXCLOSEDEVICENVPROC)(HANDLE hDevice);
typedef HANDLE(WINAPI* PFNWGLDXREGISTEROBJECTNVPROC)(HANDLE hDevice,
                                                     tgfx::HardwareBufferRef dxObject, GLuint name,
                                                     GLenum type, GLenum access);
typedef BOOL(WINAPI* PFNWGLDXUNREGISTEROBJECTNVPROC)(HANDLE hDevice, HANDLE hObject);
typedef BOOL(WINAPI* PFNWGLDXLOCKOBJECTSNVPROC)(HANDLE hDevice, GLint count, HANDLE* hObjects);
typedef BOOL(WINAPI* PFNWGLDXUNLOCKOBJECTSNVPROC)(HANDLE hDevice, GLint count, HANDLE* hObjects);

// GL_EXT_memory_object function typedefs
typedef void(GL_FUNCTION_TYPE* PFNGLCREATEMEMORYOBJECTSEXTPROC)(int n, unsigned* memoryObjects);
typedef void(GL_FUNCTION_TYPE* PFNGLDELETEMEMORYOBJECTSEXTPROC)(int n,
                                                                const unsigned* memoryObjects);
typedef void(GL_FUNCTION_TYPE* PFNGLMEMORYOBJECTPARAMETERIVEXTPROC)(unsigned memoryObject,
                                                                    unsigned pname,
                                                                    const int* params);
typedef void(GL_FUNCTION_TYPE* PFNGLTEXSTORAGEMEM2DEXTPROC)(unsigned target, int levels,
                                                            unsigned internalFormat, int width,
                                                            int height, unsigned memory,
                                                            uint64_t offset);
typedef void(GL_FUNCTION_TYPE* PFNGLIMPORTMEMORYWIN32HANDLEEXTPROC)(unsigned memory, uint64_t size,
                                                                    unsigned handleType,
                                                                    HANDLE handle);

namespace tgfx {

struct SharedInteropDevice {
  HANDLE interopDevice = nullptr;
  ID3D11Device* d3d11Device = nullptr;
  int refCount = 0;
};

struct WGLInteropState {
  // WGL_NV_DX_interop
  bool nvChecked = false;
  bool nvAvailable = false;
  PFNWGLDXOPENDEVICENVPROC wglDXOpenDeviceNV = nullptr;
  PFNWGLDXCLOSEDEVICENVPROC wglDXCloseDeviceNV = nullptr;
  PFNWGLDXREGISTEROBJECTNVPROC wglDXRegisterObjectNV = nullptr;
  PFNWGLDXUNREGISTEROBJECTNVPROC wglDXUnregisterObjectNV = nullptr;
  PFNWGLDXLOCKOBJECTSNVPROC wglDXLockObjectsNV = nullptr;
  PFNWGLDXUNLOCKOBJECTSNVPROC wglDXUnlockObjectsNV = nullptr;
  std::vector<SharedInteropDevice> sharedDevices;

  // GL_EXT_memory_object
  bool memObjChecked = false;
  bool memObjAvailable = false;
  PFNGLCREATEMEMORYOBJECTSEXTPROC glCreateMemoryObjectsEXT = nullptr;
  PFNGLDELETEMEMORYOBJECTSEXTPROC glDeleteMemoryObjectsEXT = nullptr;
  PFNGLMEMORYOBJECTPARAMETERIVEXTPROC glMemoryObjectParameterivEXT = nullptr;
  PFNGLTEXSTORAGEMEM2DEXTPROC glTexStorageMem2DEXT = nullptr;
  PFNGLIMPORTMEMORYWIN32HANDLEEXTPROC glImportMemoryWin32HandleEXT = nullptr;
};

}  // namespace tgfx
