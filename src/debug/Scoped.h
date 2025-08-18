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
#include "tgfx/core/Clock.h"

namespace tgfx::debug {
class Scoped {
 public:
  Scoped(OpTaskType type, bool isActive) : active(isActive), type(type) {
    if (!active) {
      return;
    }
    MsgPrepare(MsgType::OperateBegin);
    item.operateBegin.nsTime = Clock::Now();
    item.operateBegin.type = static_cast<uint8_t>(type);
    MsgCommit();
  }

  ~Scoped() {
    if (!active) {
      return;
    }
    MsgPrepare(MsgType::OperateEnd);
    item.operateEnd.nsTime = Clock::Now();
    item.operateEnd.type = static_cast<uint8_t>(type);
    MsgCommit();
  }

  Scoped(const Scoped&) = delete;

  Scoped(Scoped&&) = delete;

  Scoped& operator=(const Scoped&) = delete;

  Scoped& operator=(Scoped&&) = delete;

 private:
  bool active = false;
  OpTaskType type = OpTaskType::Unknown;
};
}  // namespace tgfx::debug