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


#include "WinThread.h"
#include <processthreadsapi.h>

namespace tgfx {

DWORD WINAPI WinThread::ThreadProc(LPVOID lpParameter) {
  auto thread = static_cast<WinThread*>(lpParameter);
  if (thread->task) {
    thread->task();
  }
  return 0;
}

WinThread::~WinThread() {
  if (WinThread::joinable()) {
    CloseHandle(threadHandle);
  }
}


void WinThread::onStart() {
  threadHandle = CreateThread(
      nullptr,
      0,
      ThreadProc,
      this,
      CREATE_SUSPENDED,
      &threadID);

  if (threadHandle) {
    setThreadPriority();
    ResumeThread(threadHandle);
  }
}

void WinThread::onJoin() {
  if (joinable()) {
    WaitForSingleObject(threadHandle, INFINITE);
    CloseHandle(threadHandle);
    threadHandle = INVALID_HANDLE_VALUE;
  }
}

bool WinThread::joinable() const {
  return threadHandle != INVALID_HANDLE_VALUE;
}

// 设置Windows线程优先级
void WinThread::setThreadPriority() {
  int winPriority = THREAD_PRIORITY_NORMAL;
  switch (priority) {
    case Priority::Lowest:  winPriority = THREAD_PRIORITY_LOWEST; break;
    case Priority::Low:     winPriority = THREAD_PRIORITY_BELOW_NORMAL; break;
    case Priority::Normal:  winPriority = THREAD_PRIORITY_NORMAL; break;
    case Priority::High:    winPriority = THREAD_PRIORITY_ABOVE_NORMAL; break;
    case Priority::Highest: winPriority = THREAD_PRIORITY_HIGHEST; break;
  }
  SetThreadPriority(threadHandle, winPriority);
}

Thread* Thread::Create(std::function<void()> task, Priority priority) {
  return new WinThread(std::move(task), priority);
}

} // namespace tgfx
