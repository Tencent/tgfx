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
#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif
#if defined _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <malloc.h>
#include <windows.h>
#include "TracyUwp.hpp"
#else
#include <pthread.h>
#include <unistd.h>
#include <string>
#endif
#ifdef __MINGW32__
#define __STDC_FORMAT_MACROS
#endif

#include <cinttypes>
#include "Utils.h"

namespace inspector {

uint32_t GetThreadHandleImpl() {

#if defined _WIN32
  static_assert(sizeof(decltype(GetCurrentThreadId())) <= sizeof(uint32_t),
                "Thread handle too big to fit in protocol");
  return uint32_t(GetCurrentThreadId());
#elif defined __APPLE__
  uint64_t id;
  pthread_threadid_np(pthread_self(), &id);
  return uint32_t(id);
#elif defined __EMSCRIPTEN__
  return pthread_self();
#else
  // To add support for a platform, retrieve and return the kernel thread identifier here.
  //
  // Note that pthread_t (as for example returned by pthread_self()) is *not* a kernel
  // thread identifier. It is a pointer to a library-allocated data structure instead.
  // Such pointers will be reused heavily, making the pthread_t non-unique. Additionally
  // a 64-bit pointer cannot be reliably truncated to 32 bits.
#error "Unsupported platform!"
#endif
}

const char* GetEnvVar(const char* name) {
#if defined _WIN32
  static char buffer[1024];
  DWORD const kBufferSize = DWORD(sizeof(buffer) / sizeof(buffer[0]));
  DWORD count = GetEnvironmentVariableA(name, buffer, kBufferSize);

  if (count == 0) {
    return nullptr;
  }

  if (count >= kBufferSize) {
    char* buf = reinterpret_cast<char*>(_alloca(count + 1));
    count = GetEnvironmentVariableA(name, buf, count + 1);
    memcpy(buffer, buf, kBufferSize);
    buffer[kBufferSize - 1] = 0;
  }

  return buffer;
#else
  return getenv(name);
#endif
}

uint64_t GetPid() {
#if defined _WIN32
  return uint64_t(GetCurrentProcessId());
#else
  return uint64_t(getpid());
#endif
}

const char* GetProcessName() {
  const char* processName = "unknown";
#ifdef _WIN32
  static char buf[_MAX_PATH];
  GetModuleFileNameA(nullptr, buf, _MAX_PATH);
  const char* ptr = buf;
  while (*ptr != '\0') ptr++;
  while (ptr > buf && *ptr != '\\' && *ptr != '/') ptr--;
  if (ptr > buf) ptr++;
  processName = ptr;
#elif defined __APPLE__ || defined BSD
  auto buf = getprogname();
  if (buf) {
    processName = buf;
  }
#endif
  return processName;
}

BroadcastMessage GetBroadcastMessage(const char* procname, size_t pnsz, size_t& len,
                                      uint16_t port, uint8_t type) {
  BroadcastMessage msg;
  msg.type = type;
  msg.protocolVersion = ProtocolVersion;
  msg.listenPort = port;
  msg.pid = GetPid();

  memcpy(msg.programName, procname, pnsz);
  memset(msg.programName + pnsz, 0, WelcomeMessageProgramNameSize - pnsz);
  len = offsetof(BroadcastMessage, programName) + pnsz + 1;
  return msg;
}

}  // namespace inspector