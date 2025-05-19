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

#if defined _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#pragma once
#include <thread>
namespace inspector {

#if defined _WIN32

class Thread {
 public:
  Thread(void (*func)(void* ptr), void* ptr)
      : func(func), ptr(ptr), hnd(CreateThread(nullptr, 0, Launch, this, 0, nullptr)) {
  }

  ~Thread() {
    WaitForSingleObject(hnd, INFINITE);
    CloseHandle(hnd);
  }

  HANDLE Handle() const {
    return hnd;
  }

 private:
  static DWORD WINAPI Launch(void* ptr) {
    ((Thread*)ptr)->func(((Thread*)ptr)->ptr);
    return 0;
  }

  void (*func)(void* ptr);
  void* ptr;
  HANDLE hnd;
};

#else

class Thread {
 public:
  Thread(void (*func)(void* ptr), void* ptr) : func(func), ptr(ptr) {
    pthread_create(&thread, nullptr, Launch, this);
  }

  ~Thread() {
    pthread_join(thread, nullptr);
  }

  pthread_t Handle() const {
    return thread;
  }

 private:
  static void* Launch(void* ptr) {
    ((Thread*)ptr)->func(((Thread*)ptr)->ptr);
    return nullptr;
  }
  void (*func)(void* ptr);
  void* ptr;
  pthread_t thread;
};

#endif

}  // namespace inspector