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
#include <unordered_map>
#include <vector>
#include <tgfx/core/Pixmap.h>

#include "Protocol.h"
#include "tgfx/core/Data.h"
#include "tgfx/gpu/PixelFormat.h"

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
  uint8_t continuous = 1;
};

struct OpTaskData {
  int64_t start = 0;
  int64_t end = 0;
  uint32_t id = 0;
  uint8_t type = OpTaskType::Unknown;
};
enum { OpTaskDataSize = sizeof(OpTaskData) };

static std::unordered_map<uint8_t, const char*> OpTaskName = {
    {OpTaskType::Unknown, "Unknown"},
    {OpTaskType::Flush, "Flush"},
    {OpTaskType::ResourceTask, "ResourceTask"},
    {OpTaskType::TextureUploadTask, "TextureUploadTask"},
    {OpTaskType::ShapeBufferUploadTask, "ShapeBufferUploadTask"},
    {OpTaskType::GpuUploadTask, "GpuUploadTask"},
    {OpTaskType::TextureCreateTask, "TextureCreateTask"},
    {OpTaskType::RenderTargetCreateTask, "RenderTargetCreateTask"},
    {OpTaskType::TextureFlattenTask, "TextureFlattenTask"},
    {OpTaskType::RenderTask, "RenderTask"},
    {OpTaskType::RenderTargetCopyTask, "RenderTargetCopyTask"},
    {OpTaskType::RuntimeDrawTask, "RuntimeDrawTask"},
    {OpTaskType::TextureResolveTask, "TextureResolveTask"},
    {OpTaskType::OpsRenderTask, "OpsRenderTask"},
    {OpTaskType::ClearOp, "ClearOp"},
    {OpTaskType::RectDrawOp, "RectDrawOp"},
    {OpTaskType::RRectDrawOp, "RRectDrawOp"},
    {OpTaskType::ShapeDrawOp, "ShapeDrawOp"},
    {OpTaskType::DstTextureCopyOp, "DstTextureCopyOp"},
    {OpTaskType::ResolveOp, "ResolveOp"},
    {OpTaskType::OpTaskTypeSize, "OpTaskTypeSize"},
};

static std::unordered_map<tgfx::PixelFormat, const char*> PixelFormatName = {
    {tgfx::PixelFormat::Unknown, "Unknown"},     {tgfx::PixelFormat::ALPHA_8, "ALPHA_8"},
    {tgfx::PixelFormat::GRAY_8, "GRAY_8"},       {tgfx::PixelFormat::RG_88, "RG_88"},
    {tgfx::PixelFormat::RGBA_8888, "RGBA_8888"}, {tgfx::PixelFormat::BGRA_8888, "BGRA_8888"},
};

enum DataType : uint8_t { Color, Vec4, Mat4, Int, Uint32, Bool, Float, Enum, String, Count };
enum OpOrTask : uint8_t { Op, Task, NoType };

static std::unordered_map<TGFXEnum, std::vector<std::string>> TGFXEnumName = {
    {TGFXEnum::BufferType, {"Index", "Vertex"}},
    {TGFXEnum::BlendMode,
     {"Clear",       "Src",       "Dst",        "SrcOver",   "DstOver",    "SrcIn",
      "DstIn",       "SrcOut",    "DstOut",     "SrcTop",    "DstTop",     "Xor",
      "PlusLighter", "Modulate",  "Screen",     "OverLay",   "Darken",     "Lighten",
      "ColorDodge",  "ColorBurn", "HardLight",  "SoftLight", "Difference", "Exclusion",
      "Multiply",    "Hue",       "Saturation", "Color",     "Luminosity", "PlusDarker"}},
    {TGFXEnum::AAType, {"None", "Coverage", "MSAA"}},
    {TGFXEnum::PixelFormat, {"Unknown", "ALPHA_8", "GRAY_8", "RG_88", "RGBA_8888", "BGRA_8888"}},
    {TGFXEnum::ImageOrigin, {"TopLeft", "BottomLeft"}},
};

struct DataHead {
  DataType type;
  uint64_t name;
};

struct PropertyData {
  std::vector<DataHead> summaryName;
  std::vector<DataHead> processName;
  std::vector<std::shared_ptr<tgfx::Data>> summaryData;
  std::vector<std::shared_ptr<tgfx::Data>> processData;
};

struct TextureData {
  std::vector<std::shared_ptr<tgfx::Data>> inputTextures;
  std::shared_ptr<tgfx::Data> outputTexture;
  std::vector<std::shared_ptr<tgfx::Pixmap>> inputTexture;
  std::shared_ptr<tgfx::Pixmap> outputTextures;
};

struct VertexData {
  std::vector<float> vertexData;
  bool hasUV;
  bool hasColor;
};

OpOrTask getOpTaskType(OpTaskType type);
}  // namespace inspector