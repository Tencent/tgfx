/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "Semaphore.h"

namespace tgfx {
void Semaphore::signal() {
//  std::lock_guard<std::mutex> autoLock(locker);
  count++;
  condition.notify_one();
}

void Semaphore::wait() {
//  std::unique_lock<std::mutex> autoLock(locker);
  if (count <= 0) {
//    condition.wait(autoLock);
  }
  count--;
}
}  // namespace tgfx