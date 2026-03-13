/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "core/ColorSpaceXformSteps.h"
#include "core/utils/BlockAllocator.h"
#include "core/utils/PlacementPtr.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"

namespace tgfx {
/**
 * A provider for instance data used in instanced drawing. It holds matrices and optional per-
 * instance colors, and writes them as GPU-ready instance records into a given buffer.
 */
class InstanceProvider {
 public:
  /**
   * Creates an InstanceProvider that generates instance records from the given matrices and optional
   * colors. If colors is not nullptr, each instance record will include a color field. The
   * colorSpace is used to convert colors from sRGB to the destination color space.
   */
  static PlacementPtr<InstanceProvider> MakeFrom(BlockAllocator* allocator, const Matrix* matrices,
                                                 const Color* colors, size_t count,
                                                 const std::shared_ptr<ColorSpace>& colorSpace);

  /**
   * Returns the total size in bytes of the instance data.
   */
  size_t dataSize() const {
    return _dataSize;
  }

  /**
   * Returns true if the instance records contain per-instance colors.
   */
  bool hasColors() const {
    return _hasColors;
  }

  /**
   * Writes instance data into the given buffer. The buffer must be at least dataSize() bytes.
   */
  void getData(void* buffer) const;

 private:
  InstanceProvider(std::shared_ptr<BlockAllocator> reference, const Matrix* matrices,
                   const Color* colors, size_t count, size_t dataSize, bool hasColors,
                   std::unique_ptr<ColorSpaceXformSteps> steps);

  std::shared_ptr<BlockAllocator> reference = nullptr;
  std::unique_ptr<ColorSpaceXformSteps> xformSteps = nullptr;
  const Matrix* matrices = nullptr;
  const Color* colors = nullptr;
  size_t count = 0;
  size_t _dataSize = 0;
  bool _hasColors = false;

  friend class BlockAllocator;
};
}  // namespace tgfx
