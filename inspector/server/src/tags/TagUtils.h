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
#include <cstdint>
#include "DecodeStream.h"
#include "EncodeStream.h"
#include "InspectorEvent.h"

namespace inspector {
int64_t ReadTimeOffset(DecodeStream* stream, int64_t& refTime);
void WriteTimeOffset(EncodeStream* stream, int64_t& refTime, int64_t time);

void ReadDataHead(std::vector<DataHead>& dataHead, DecodeStream* stream);
void WriteDataHead(const std::vector<DataHead>& dataHead, EncodeStream* stream);
}  // namespace inspector