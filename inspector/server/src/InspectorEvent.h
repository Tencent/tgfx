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
#include <tgfx/core/Data.h>
#include <cstdint>
#include <vector>
#include "Protocol.h"

namespace inspector {

#define SPATIAL_PRECISION 0.05f
#define BEZIER_PRECISION 0.005f
#define GRADIENT_PRECISION 0.00002f

struct StringLocation {
  const char* ptr;
  uint32_t idx;
};

struct FrameEvent {
  int64_t start;
  int64_t end;
  int64_t drawCall;
  int64_t triangles;
  int32_t frameImage;
};
enum { FrameEventSize = sizeof(FrameEvent) };

struct FrameData {
  std::vector<FrameEvent> frames;
  uint8_t continuous = 0;

  int64_t min = std::numeric_limits<int64_t>::max();
  int64_t max = std::numeric_limits<int64_t>::min();
  int64_t total = 0;
  double sumSq = 0;
};

struct OpTaskData {
  int64_t start = 0;
  int64_t end = 0;
  uint32_t id = 0;
  uint8_t type = OpTaskType::Unknown;
};
enum { OpTaskDataSize = sizeof(OpTaskData) };

enum DataType : uint8_t { Color, Vect, Mat4, Int, Float, String, Count };

struct DataHead {
  DataType type;
  uint16_t size;
};

struct PropertyData {
  std::vector<DataHead> summaryName;
  std::vector<DataHead> processName;
  std::vector<uint8_t> summaryData;
  std::vector<uint8_t> processData;
};

struct TextureData {
  std::vector<std::shared_ptr<tgfx::Data>> inputTextures;
  std::shared_ptr<tgfx::Data> outputTexture;
};

struct VertexData {
  std::vector<float> vertexData;
  bool hasUV;
  bool hasColor;
};
}  // namespace inspector