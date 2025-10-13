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
#else
#include <pthread.h>
#include <unistd.h>
#endif
#ifdef __MINGW32__
#define __STDC_FORMAT_MACROS
#endif

#include <cinttypes>
#include <cstddef>
#include <cstring>
#include <string>
#include "ProcessUtils.h"

namespace tgfx::inspect {
uint64_t GetPid() {
#if defined _WIN32
  return uint64_t(GetCurrentProcessId());
#else
  return uint64_t(getpid());
#endif
}

const char* GetProcessName() {
  static std::string processName = "unknown";
  if (processName != "unknown") {
    return processName.c_str();
  }
#ifdef _WIN32
  char buf[_MAX_PATH] = {0};
  GetModuleFileNameA(nullptr, buf, _MAX_PATH);
  const char* ptr = buf;
  while (*ptr != '\0') {
    ptr++;
  }
  while (ptr > buf && *ptr != '\\' && *ptr != '/') {
    ptr--;
  }
  if (ptr > buf) {
    ptr++;
  }
  processName = ptr;
#elif defined __APPLE__ || defined BSD
  auto buf = getprogname();
  if (buf) {
    processName = buf;
  }
#endif
  return processName.c_str();
}

BroadcastMessage GetBroadcastMessage(const char* procname, size_t pnsz, size_t& len, uint16_t port,
                                     ToolType type) {
  BroadcastMessage msg = {};
  msg.type = static_cast<uint8_t>(type);
  msg.protocolVersion = ProtocolVersion;
  msg.listenPort = port;
  msg.pid = GetPid();

  memcpy(msg.programName, procname, pnsz);
  memset(msg.programName + pnsz, 0, WelcomeMessageProgramNameSize - pnsz);
  len = offsetof(BroadcastMessage, programName) + pnsz + 1;
  return msg;
}

}  // namespace tgfx::inspect