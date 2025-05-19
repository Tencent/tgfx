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
#include <unordered_map>
#include "DecodeStream.h"
#include "InspectorEvent.h"
#include "TagHeader.h"

namespace inspector {
void ReadPropertyTag(DecodeStream* stream);

TagType WritePropertyTag(EncodeStream* stream, std::unordered_map<uint32_t, std::shared_ptr<PropertyData>>* properties);
}