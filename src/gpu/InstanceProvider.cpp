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

struct InstanceRecord {
  float offset[2];  // [dx, dy]
};

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
  size_t instanceStride = hasColors ? sizeof(InstanceRecordWithColor) : sizeof(InstanceRecord);
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
  size_t instanceStride = _hasColors ? sizeof(InstanceRecordWithColor) : sizeof(InstanceRecord);
  auto steps = xformSteps.get();
  for (size_t i = 0; i < count; i++) {
    auto record = static_cast<InstanceRecord*>(buffer);
    record->offset[0] = offsets[i].x;
    record->offset[1] = offsets[i].y;
    if (_hasColors) {
      auto colorRecord = static_cast<InstanceRecordWithColor*>(buffer);
      colorRecord->color = ToUintPMColor(colors[i], steps);
    }
    buffer = static_cast<uint8_t*>(buffer) + instanceStride;
  }
}

}  // namespace tgfx
