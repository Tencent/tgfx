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
  float matrixCol0[2];  // [scaleX, skewY]
  float matrixCol1[2];  // [skewX, scaleY]
  float matrixCol2[2];  // [transX, transY]
};

struct InstanceRecordWithColor {
  float matrixCol0[2];
  float matrixCol1[2];
  float matrixCol2[2];
  uint32_t color;  // UByte4Normalized premultiplied RGBA
};

static void FillInstanceRecord(void* buffer, const Matrix& matrix) {
  auto record = static_cast<InstanceRecord*>(buffer);
  record->matrixCol0[0] = matrix.getScaleX();
  record->matrixCol0[1] = matrix.getSkewY();
  record->matrixCol1[0] = matrix.getSkewX();
  record->matrixCol1[1] = matrix.getScaleY();
  record->matrixCol2[0] = matrix.getTranslateX();
  record->matrixCol2[1] = matrix.getTranslateY();
}

PlacementPtr<InstanceProvider> InstanceProvider::MakeFrom(
    BlockAllocator* allocator, const Matrix* matrices, const Color* colors, size_t count,
    const std::shared_ptr<ColorSpace>& colorSpace) {
  if (allocator == nullptr || matrices == nullptr || count == 0) {
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
  return allocator->make<InstanceProvider>(allocator->addReference(), matrices, colors, count,
                                           dataSize, hasColors, std::move(steps));
}

InstanceProvider::InstanceProvider(std::shared_ptr<BlockAllocator> reference,
                                   const Matrix* matrices, const Color* colors, size_t count,
                                   size_t dataSize, bool hasColors,
                                   std::unique_ptr<ColorSpaceXformSteps> steps)
    : reference(std::move(reference)), xformSteps(std::move(steps)), matrices(matrices),
      colors(colors), count(count), _dataSize(dataSize), _hasColors(hasColors) {
}

void InstanceProvider::getData(void* buffer) const {
  size_t instanceStride = _hasColors ? sizeof(InstanceRecordWithColor) : sizeof(InstanceRecord);
  auto steps = xformSteps.get();
  for (size_t i = 0; i < count; i++) {
    FillInstanceRecord(buffer, matrices[i]);
    if (_hasColors) {
      auto colorRecord = static_cast<InstanceRecordWithColor*>(buffer);
      colorRecord->color = ToUintPMColor(colors[i], steps);
    }
    buffer = static_cast<uint8_t*>(buffer) + instanceStride;
  }
}

}  // namespace tgfx
