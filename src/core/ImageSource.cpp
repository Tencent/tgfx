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

#include "ImageSource.h"

#include <utility>

namespace tgfx {
std::unique_ptr<DataSource<ImageBuffer>> ImageSource::MakeFrom(
    std::shared_ptr<ImageGenerator> generator, bool tryHardware, bool asyncDecoding) {
  if (generator == nullptr) {
    return nullptr;
  }
  if (asyncDecoding && !generator->asyncSupport()) {
    // The generator may have built-in async decoding support which will not block the main thread.
    // Therefore, we should trigger the decoding ASAP.
    auto buffer = generator->makeBuffer(tryHardware);
    return Wrap(std::move(buffer));
  }
  auto imageSource = std::make_unique<ImageSource>(std::move(generator), tryHardware);
  if (asyncDecoding) {
    return Async(std::move(imageSource));
  }
  return imageSource;
}

ImageSource::ImageSource(std::shared_ptr<ImageGenerator> generator, bool tryHardware)
    : generator(std::move(generator)), tryHardware(tryHardware) {
}
}  // namespace tgfx
