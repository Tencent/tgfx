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

#include "Inspector.h"
#include "LayerProfiler.h"
#include "Scoped.h"

#define SEND_LAYER_DATA(data) inspector::LayerProfiler::Get().setData(data)
#define LAYER_CALLBACK(func) inspector::LayerProfiler::Get().setCallBack(func)

#define FrameMark inspector::Inspector::SendFrameMark(nullptr)

#define ScopedMark(type, active) inspector::Scoped scoped(type, active)
#define OperateMark(type) ScopedMark(type, true)
#define TaskMark(type) ScopedMark(type, true)

#define AttributeName(name, value) inspector::Inspector::SendAttributeData(name, value)
#define AttributeNameEnum(name, value, type)                                 \
  inspector::Inspector::SendAttributeData(name, static_cast<uint8_t>(value), \
                                          static_cast<uint8_t>(type))
#define AttributeEnum(value, type) AttributeNameEnum(#value, value, type)