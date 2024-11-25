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
#define TRACE_EVENT_COLOR(color) ZoneScopedNC(TRACE_FUNC, color)

#define FRAME_MARK FrameMark

#define TRACE_THREAD_NAME(name) tracy::SetThreadName(name)
#else
#define TRACE_EVENT
#define TRACE_EVENT_COLOR(color)

#define FRAME_MARK

#define TRACE_THREAD_NAME(name)
#endif