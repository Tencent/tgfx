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
class FunctionStat {
 public:
  FunctionStat(OpTaskType type, bool isActive) : active(isActive), type(type) {
    if (!active || !FrameCapture::GetInstance().isConnected()) {
      return;
    }
    auto item = FrameCaptureMessageItem();
    item.hdr.type = FrameCaptureMessageType::OperateBegin;
    item.operateBegin.usTime = Clock::Now();
    item.operateBegin.type = static_cast<uint8_t>(type);
    FrameCapture::GetInstance().queueSerialFinish(item);
  }

  ~FunctionStat() {
    if (!active || !FrameCapture::GetInstance().isConnected()) {
      return;
    }
    auto item = FrameCaptureMessageItem();
    item.hdr.type = FrameCaptureMessageType::OperateEnd;
    item.operateEnd.usTime = Clock::Now();
    item.operateEnd.type = static_cast<uint8_t>(type);
    FrameCapture::GetInstance().queueSerialFinish(item);
  }

  FunctionStat(const FunctionStat&) = delete;

  FunctionStat(FunctionStat&&) = delete;

  FunctionStat& operator=(const FunctionStat&) = delete;

  FunctionStat& operator=(FunctionStat&&) = delete;

 private:
  bool active = false;
  OpTaskType type = OpTaskType::Unknown;
};
}  // namespace tgfx::inspect
