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
#include <memory>

namespace tgfx::inspect {
class LZ4CompressionHandler {
 public:
  static std::unique_ptr<LZ4CompressionHandler> Make();

  static size_t GetMaxOutputSize(size_t inputSize);

  virtual ~LZ4CompressionHandler() = default;

  virtual size_t encode(uint8_t* dstBuffer, size_t dstSize, const uint8_t* srcBuffer,
                        size_t srcSize) const = 0;
};
}  // namespace tgfx::inspect
