/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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
#ifdef TGFX_ENABLE_PROFILING
#define TRACY_ENABLE
#include "public/common/TracySystem.hpp"
#include "public/tracy/Tracy.hpp"

#if defined(_MSC_VER)
#define TRACE_FUNC __FUNCSIG__
#else
#define TRACE_FUNC __PRETTY_FUNCTION__
#endif

#define TRACE_EVENT ZoneScopedN(TRACE_FUNC)
#define TRACE_EVENT_NAME(name) ZoneScopedN(name)
#define TRACE_EVENT_COLOR(color) ZoneScopedNC(TRACE_FUNC, color)

#define TRACE_DRAWCALL FrameData(nullptr, tracy::FrameDataType::DrawCall, 1)
#define TRACE_TRANGLES(num) FrameData(nullptr, tracy::FrameDataType::Trangles, num)
#define TRACE_DRAW(tranglesNum)  \
  {                              \
    TRACE_DRAWCALL;              \
    TRACE_TRANGLES(tranglesNum); \
  }

#define FRAME_MARK FrameMark
#define FRAME_MARK_START FrameMarkStart(nullptr)
#define FRAME_MARK_END FrameMarkEnd(nullptr)

static constexpr uint32_t INVALID_THREAD_NUMBER = 0;
inline uint32_t GetThreadNumber() {
  static std::atomic<uint32_t> nextID{1};
  uint32_t number;
  do {
    number = nextID.fetch_add(1, std::memory_order_relaxed);
  } while (number == INVALID_THREAD_NUMBER);
  return number;
}

inline std::string GetThreadName() {
  char threadName[10] = {'\0'};
  snprintf(threadName, 10, "Thread_%d", GetThreadNumber());
  return threadName;
}

#define TRACE_THREAD tracy::SetThreadName(GetThreadName().c_str())
#else
#define TRACE_EVENT
#define TRACE_EVENT_NAME(name)
#define TRACE_EVENT_COLOR(color)

#define TRACE_DRAWCALL
#define TRACE_TRANGLES(num)
#define TRACE_DRAW(tranglesNum)

#define FRAME_MARK
#define FRAME_MARK_START
#define FRAME_MARK_END

#define TRACE_THREAD
#endif