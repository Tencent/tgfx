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
#include "StreamContext.h"
#include "StringDiscovery.h"
#include "InspectorEvent.h"

namespace inspector {
class DataContext: public StreamContext {
public:
  std::mutex lock;
  StringDiscovery<FrameData*> frames;
  std::vector<std::shared_ptr<OpTaskData>> opTasks;
  std::vector<std::shared_ptr<OpTaskData>> opTaskStack;
  std::unordered_map<uint32_t, std::vector<uint32_t>> opChilds;
  std::unordered_map<uint32_t, std::shared_ptr<PropertyData>> properties;
  std::unordered_map<uint32_t, std::shared_ptr<TextureData>> textures;
  std::unordered_map<uint32_t, std::shared_ptr<VertexData>> vertexDatas;
  FrameData* framebase = nullptr;
  uint64_t opTaskCount = 0;
  int64_t baseTime = 0;
  int64_t lastTime = 0;
};
}
