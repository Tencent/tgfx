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
#include "FrameCapture.h"
#include "tgfx/core/Clock.h"

namespace tgfx::inspect {

static std::unordered_map<uint8_t, const char*> OpTaskName = {
    {static_cast<uint8_t>(tgfx::inspect::OpTaskType::Unknown), "Unknown"},
    {static_cast<uint8_t>(tgfx::inspect::OpTaskType::Flush), "Flush"},
    {static_cast<uint8_t>(tgfx::inspect::OpTaskType::ResourceTask), "ResourceTask"},
    {static_cast<uint8_t>(tgfx::inspect::OpTaskType::TextureUploadTask), "TextureUploadTask"},
    {static_cast<uint8_t>(tgfx::inspect::OpTaskType::ShapeBufferUploadTask),
     "ShapeBufferUploadTask"},
    {static_cast<uint8_t>(tgfx::inspect::OpTaskType::GpuUploadTask), "GpuUploadTask"},
    {static_cast<uint8_t>(tgfx::inspect::OpTaskType::TextureCreateTask), "TextureCreateTask"},
    {static_cast<uint8_t>(tgfx::inspect::OpTaskType::RenderTargetCreateTask),
     "RenderTargetCreateTask"},
    {static_cast<uint8_t>(tgfx::inspect::OpTaskType::TextureFlattenTask), "TextureFlattenTask"},
    {static_cast<uint8_t>(tgfx::inspect::OpTaskType::RenderTask), "RenderTask"},
    {static_cast<uint8_t>(tgfx::inspect::OpTaskType::RenderTargetCopyTask), "RenderTargetCopyTask"},
    {static_cast<uint8_t>(tgfx::inspect::OpTaskType::RuntimeDrawTask), "RuntimeDrawTask"},
    {static_cast<uint8_t>(tgfx::inspect::OpTaskType::TextureResolveTask), "TextureResolveTask"},
    {static_cast<uint8_t>(tgfx::inspect::OpTaskType::OpsRenderTask), "OpsRenderTask"},
    {static_cast<uint8_t>(tgfx::inspect::OpTaskType::ClearOp), "ClearOp"},
    {static_cast<uint8_t>(tgfx::inspect::OpTaskType::RectDrawOp), "RectDrawOp"},
    {static_cast<uint8_t>(tgfx::inspect::OpTaskType::RRectDrawOp), "RRectDrawOp"},
    {static_cast<uint8_t>(tgfx::inspect::OpTaskType::ShapeDrawOp), "ShapeDrawOp"},
    {static_cast<uint8_t>(tgfx::inspect::OpTaskType::DstTextureCopyOp), "DstTextureCopyOp"},
    {static_cast<uint8_t>(tgfx::inspect::OpTaskType::ResolveOp), "ResolveOp"},
    {static_cast<uint8_t>(tgfx::inspect::OpTaskType::OpTaskTypeSize), "OpTaskTypeSize"},
};

class FunctionTimer {
 public:
  FunctionTimer(OpTaskType type, bool isActive) : active(isActive), type(type) {
    if (!active) {
      return;
    }
    auto item = FrameCaptureMessageItem();
    item.hdr.type = FrameCaptureMessageType::OperateBegin;
    item.operateBegin.usTime = Clock::Now();
    item.operateBegin.type = static_cast<uint8_t>(type);
    FrameCapture::QueueSerialFinish(item);
  }

  ~FunctionTimer() {
    if (!active) {
      return;
    }
    auto item = FrameCaptureMessageItem();
    item.hdr.type = FrameCaptureMessageType::OperateEnd;
    item.operateEnd.usTime = Clock::Now();
    item.operateEnd.type = static_cast<uint8_t>(type);
    FrameCapture::QueueSerialFinish(item);
  }

  FunctionTimer(const FunctionTimer&) = delete;

  FunctionTimer(FunctionTimer&&) = delete;

  FunctionTimer& operator=(const FunctionTimer&) = delete;

  FunctionTimer& operator=(FunctionTimer&&) = delete;

 private:
  bool active = false;
  OpTaskType type = OpTaskType::Unknown;
};
}  // namespace tgfx::inspect