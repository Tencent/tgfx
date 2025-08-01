/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "core/utils/BlockBuffer.h"
#include "core/utils/UniqueID.h"
#include "tgfx/core/BytesKey.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {
#define DEFINE_PROCESSOR_CLASS_ID               \
  static uint32_t ClassID() {                   \
    static uint32_t ClassID = UniqueID::Next(); \
    return ClassID;                             \
  }

class Processor {
 public:
  explicit Processor(uint32_t classID) : _classID(classID) {
  }

  virtual ~Processor() = default;

  /**
   * Human-meaningful string to identify this processor.
   */
  virtual std::string name() const = 0;

  uint32_t classID() const {
    return _classID;
  }

  virtual void computeProcessorKey(Context* context, BytesKey* bytesKey) const = 0;

 private:
  uint32_t _classID = 0;
};
}  // namespace tgfx
