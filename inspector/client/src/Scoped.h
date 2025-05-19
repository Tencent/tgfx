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
#include "Inspector.h"

namespace inspector {
class Scoped {

 public:
  Scoped(const Scoped&) = delete;
  Scoped(Scoped&&) = delete;
  Scoped& operator=(const Scoped&) = delete;
  Scoped& operator=(Scoped&&) = delete;

  Scoped(uint8_t type, bool isActive) : active(isActive) {
    if (!active) {
      return;
    }
    QueuePrepare(QueueType::OperateBegin);
    MemWrite(&item->operateBegin.time, Inspector::GetTime());
    MemWrite(&item->operateBegin.type, type);
    QueueCommit(operateBegin);
  }

  ~Scoped() {
    if (active) {
      return;
    }
    QueuePrepare(QueueType::OperateEnd);
    MemWrite(&item->operateEnd.time, Inspector::GetTime());
    QueueCommit(operateEnd);
  }

 private:
  bool active;
};
}  // namespace inspector