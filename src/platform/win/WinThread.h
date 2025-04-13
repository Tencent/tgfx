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
#include <windows.h>
#include "core/utils/Thread.h"

namespace tgfx {

class WinThread : public Thread {
 public:
  explicit WinThread(std::function<void()> task, Priority priority)
      : Thread(std::move(task), priority) {}

  ~WinThread() override {
    if (joinable()) {
      CloseHandle(threadHandle);
    }
  }

  void onStart() override;
  void onJoin() override;
  bool joinable() const override;

 private:
  static DWORD WINAPI ThreadProc(LPVOID lpParameter);
  void setThreadPriority();
  HANDLE threadHandle = INVALID_HANDLE_VALUE;
  DWORD threadID = 0;
};

} // namespace tgfx
#endif //WINTHREAD_H
