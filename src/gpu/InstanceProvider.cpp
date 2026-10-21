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

#include "InstanceProvider.h"
#include "core/utils/ColorHelper.h"
#include "core/utils/ColorSpaceHelper.h"

namespace tgfx {

struct InstanceRecordWithColor {
  float offset[2];  // [dx, dy]
  uint32_t color;   // UByte4Normalized premultiplied RGBA
};

PlacementPtr<InstanceProvider> InstanceProvider::MakeFrom(
    BlockAllocator* allocator, const Point* offsets, const Color* colors, size_t count,
    const std::shared_ptr<ColorSpace>& colorSpace) {
  if (allocator == nullptr || offsets == nullptr || count == 0) {
    return nullptr;
  }
  bool hasColors = colors != nullptr;
  // The record always carries the color slot: colorless batches broadcast white, matching the
  // runtime codegen's implicit color for instanced shapes without per-instance colors.
  size_t instanceStride = sizeof(InstanceRecordWithColor);
  size_t dataSize = instanceStride * count;
  std::unique_ptr<ColorSpaceXformSteps> steps = nullptr;
  if (hasColors && NeedConvertColorSpace(ColorSpace::SRGB(), colorSpace)) {
    steps =
        std::make_unique<ColorSpaceXformSteps>(ColorSpace::SRGB().get(), AlphaType::Premultiplied,
                                               colorSpace.get(), AlphaType::Premultiplied);
  }
  return allocator->make<InstanceProvider>(allocator->addReference(), offsets, colors, count,
                                           dataSize, hasColors, std::move(steps));
}

InstanceProvider::InstanceProvider(std::shared_ptr<BlockAllocator> reference, const Point* offsets,
                                   const Color* colors, size_t count, size_t dataSize,
                                   bool hasColors, std::unique_ptr<ColorSpaceXformSteps> steps)
    : reference(std::move(reference)), xformSteps(std::move(steps)), offsets(offsets),
      colors(colors), count(count), _dataSize(dataSize), _hasColors(hasColors) {
}

void InstanceProvider::getData(void* buffer) const {
  size_t instanceStride = sizeof(InstanceRecordWithColor);
  auto steps = xformSteps.get();
  for (size_t i = 0; i < count; i++) {
    auto colorRecord = static_cast<InstanceRecordWithColor*>(buffer);
    colorRecord->offset[0] = offsets[i].x;
    colorRecord->offset[1] = offsets[i].y;
    // White is color-space invariant, so the broadcast skips the conversion steps.
    colorRecord->color = _hasColors ? ToUintPMColor(colors[i], steps) : 0xFFFFFFFFu;
    buffer = static_cast<uint8_t*>(buffer) + instanceStride;
  }
}

}  // namespace tgfx
