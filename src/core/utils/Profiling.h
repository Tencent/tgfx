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
#ifdef TGFX_USE_INSPECTOR
#include "Define.h"
#else
#define FrameMark

#define ScopedMark(type, active)
#define OperateMark(type)
#define TaskMark(type)

#define AttributeName(name, value)
#define AttributeTGFXName(name, value)
#define AttributeNameFloatArray(name, value, size)
#define AttributeNameEnum(name, value, type)
#define AttributeEnum(value, type)

#define SEND_LAYER_DATA(data)
#define LAYER_CALLBACK(x)
#endif