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
#define TRACY_NO_INVARIANT_CHECK 1

#include "public/common/TracySystem.hpp"
#include "public/tracy/Tracy.hpp"

#define TRACE_EVENT(name) ZoneScopedN(name)
#define TRACE_EVENT_COLOR(name, color) ZoneScopedNC(name, color)

#define FRAME_MARK FrameMark
#define FRAME_MARK_NAME(name) FrameMarkNamed(name)
#define FRAME_MARK_START(name) FrameMarkStart(name)
#define FRAME_MARK_END(name) FrameMarkEnd(name)

#define TRACE_THREAD_NAME(name) tracy::SetThreadName(name)
#else
#define TRACE_EVENT(name)
#define TRACE_EVENT_COLOR(name, color)

#define FRAME_MARK
#define FRAME_MARK_NAME(name)
#define FRAME_MARK_START(name)
#define FRAME_MARK_END(name)

#define TRACE_THREAD_NAME(name)
#endif