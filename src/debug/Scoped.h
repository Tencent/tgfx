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

namespace inspector {
class Scoped {

 public:
  Scoped(const Scoped&) = delete;
  Scoped(Scoped&&) = delete;
  Scoped& operator=(const Scoped&) = delete;
  Scoped& operator=(Scoped&&) = delete;

  Scoped(OpTaskType type, bool isActive) : active(isActive), type(type) {
    if (!active) {
      return;
    }
    MsgPrepare(MsgType::OperateBegin);
    MemWrite(&item.operateBegin.nsTime, tgfx::Clock::Now<std::chrono::nanoseconds>());
    MemWrite(&item.operateBegin.type, type);
    MsgCommit();
  }

  ~Scoped() {
    if (!active) {
      return;
    }
    MsgPrepare(MsgType::OperateEnd);
    MemWrite(&item.operateEnd.nsTime, tgfx::Clock::Now<std::chrono::nanoseconds>());
    MemWrite(&item.operateEnd.type, type);
    MsgCommit();
  }

 private:
  bool active = false;
  OpTaskType type = OpTaskType::Unknown;
};
}  // namespace inspector