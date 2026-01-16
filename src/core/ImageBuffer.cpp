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

#include "tgfx/core/ImageBuffer.h"
#include <memory>
#include "core/PixelBuffer.h"
#include "core/YUVHardwareBuffer.h"
#include "core/utils/ColorSpaceHelper.h"
#include "gpu/resources/YUVTextureView.h"

namespace tgfx {
/**
 * YUVBuffer represents a pixel array described in the YUV format with multiple planes.
 */
class YUVBuffer : public ImageBuffer {
 public:
  YUVBuffer(std::shared_ptr<YUVData> data, YUVFormat format, YUVColorSpace yuvColorSpace)
      : data(std::move(data)), _yuvColorSpace(yuvColorSpace),
        _colorSpace(MakeColorSpaceFromYUVColorSpace(yuvColorSpace)), format(format) {
  }

  int width() const override {
    return data->width();
  }

  int height() const override {
    return data->height();
  }

  bool isAlphaOnly() const final {
    return false;
  }

  const std::shared_ptr<ColorSpace>& colorSpace() const override;

 protected:
  std::shared_ptr<TextureView> onMakeTexture(Context* context, bool) const override {
    if (format == YUVFormat::NV12) {
      return YUVTextureView::MakeNV12(context, data.get(), _yuvColorSpace);
    }
    return YUVTextureView::MakeI420(context, data.get(), _yuvColorSpace);
  }

 private:
  std::shared_ptr<YUVData> data = nullptr;
  YUVColorSpace _yuvColorSpace = YUVColorSpace::BT601_LIMITED;
  std::shared_ptr<ColorSpace> _colorSpace = nullptr;
  YUVFormat format = YUVFormat::Unknown;
};

const std::shared_ptr<ColorSpace>& YUVBuffer::colorSpace() const {
  return _colorSpace;
}

std::shared_ptr<ImageBuffer> ImageBuffer::MakeFrom(HardwareBufferRef hardwareBuffer,
                                                   YUVColorSpace colorSpace) {
  return YUVHardwareBuffer::MakeFrom(hardwareBuffer, colorSpace);
}

std::shared_ptr<ImageBuffer> ImageBuffer::MakeFrom(HardwareBufferRef hardwareBuffer,
                                                   std::shared_ptr<ColorSpace> colorSpace) {
  return PixelBuffer::MakeFrom(hardwareBuffer, std::move(colorSpace));
}

std::shared_ptr<ImageBuffer> ImageBuffer::MakeI420(std::shared_ptr<YUVData> yuvData,
                                                   YUVColorSpace colorSpace) {
  if (yuvData == nullptr || yuvData->planeCount() != YUVData::I420_PLANE_COUNT) {
    return nullptr;
  }
  return std::make_shared<YUVBuffer>(std::move(yuvData), YUVFormat::I420, colorSpace);
}

std::shared_ptr<ImageBuffer> ImageBuffer::MakeNV12(std::shared_ptr<YUVData> yuvData,
                                                   YUVColorSpace colorSpace) {
  if (yuvData == nullptr || yuvData->planeCount() != YUVData::NV12_PLANE_COUNT) {
    return nullptr;
  }
  return std::make_shared<YUVBuffer>(std::move(yuvData), YUVFormat::NV12, colorSpace);
}
}  // namespace tgfx
