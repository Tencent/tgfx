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

namespace tgfx {
/**
 * CommandBuffer represents a sequence of commands that can be submitted to the GPU for execution.
 * A CommandBuffer is created via the CommandEncoder::finish() method, the GPU commands recorded
 * within are submitted for execution by calling the CommandQueue::submit() method.
 */
class CommandBuffer {
 public:
  virtual ~CommandBuffer() = default;
};
}  // namespace tgfx
