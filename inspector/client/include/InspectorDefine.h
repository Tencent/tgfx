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

#include "Inspector.h"
#include "LayerProfiler.h"
#include "Scoped.h"

#define FrameMark inspector::Inspector::SendFrameMark(nullptr)

#define ScopedMark(type, active) inspector::Scoped scoped(type, active)
#define OperateMark(type) ScopedMark(type, true)
#define TaskMark(type) ScopedMark(type, true)

#define LAYER_DATA(data) inspector::LayerProfiler::SendLayerData(data)
#define LAYER_CALLBACK(func) inspector::LayerProfiler::SetLayerCallBack(func)