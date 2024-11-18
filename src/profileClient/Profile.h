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
#ifdef TGFX_ENABLE_PROFILE
#include "Tracy.hpp"
#include "common/TracySystem.hpp"

#define TGFX_PROFILE_ZONE_SCOPPE ZoneScopedN
#define TGFX_PROFILE_ZONE_SCOPPE_NAME(name) ZoneScopedN(name)
#define TGFX_PROFILE_ZONE_SCOPPE_COLOR(color) ZoneScopedC(color)
#define TGFX_PROFILE_ZONE_SCOPPE_NAME_COLOR(name, color) ZoneScopedNC(name, color)

#define TGFX_PROFILE_FRAME_MARK FrameMark
#define TGFX_PROFILE_FRAME_MARK_NAME(name) FrameMarkNamed(name)
#define TGFX_PROFILE_FRAME_MARK_START(name) FrameMarkStart(name)
#define TGFX_PROFILE_FRAME_MARK_END(name) FrameMarkEnd(name)

#define TGFX_PROFILE_THREAD_NAME(name) tracy::SetThreadName(name)
#else
#define TGFX_PROFILE_ZONE_SCOPPE
#define TGFX_PROFILE_ZONE_SCOPPE_NAME(name)
#define TGFX_PROFILE_ZONE_SCOPPE_COLOR(color)
#define TGFX_PROFILE_ZONE_SCOPPE_NAME_COLOR(name, color)

#define TGFX_PROFILE_FRAME_MARK
#define TGFX_PROFILE_FRAME_MARK_NAME(name)
#define TGFX_PROFILE_FRAME_MARK_START(name)
#define TGFX_PROFILE_FRAME_MARK_END(name)

#define TGFX_PROFILE_THREAD_NAME(name)
#endif