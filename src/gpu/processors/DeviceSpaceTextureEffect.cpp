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

#include "DeviceSpaceTextureEffect.h"

namespace tgfx {
DeviceSpaceTextureEffect::DeviceSpaceTextureEffect(std::shared_ptr<TextureProxy> textureProxy,
                                                   const Matrix& uvMatrix,
                                                   std::shared_ptr<ColorSpace> colorSpace)
    : FragmentProcessor(ClassID()), textureProxy(std::move(textureProxy)), uvMatrix(uvMatrix),
      dstColorSpace(std::move(colorSpace)) {
}

void DeviceSpaceTextureEffect::onComputeProcessorKey(BytesKey* bytesKey) const {
  auto srcColorSpace = textureProxy->getTextureView()->gamutColorSpace();
  auto steps = std::make_shared<ColorSpaceXformSteps>(
      srcColorSpace.get(), AlphaType::Premultiplied, dstColorSpace.get(), AlphaType::Premultiplied);
  uint64_t xformKey = 0;
  xformKey = ColorSpaceXformSteps::XFormKey(steps.get());
  auto key = reinterpret_cast<uint32_t*>(&xformKey);
  bytesKey->write(key[0]);
  bytesKey->write(key[1]);
}

std::shared_ptr<GPUTexture> DeviceSpaceTextureEffect::onTextureAt(size_t) const {
  auto textureView = textureProxy->getTextureView();
  return textureView == nullptr ? nullptr : textureView->getTexture();
}
}  // namespace tgfx
