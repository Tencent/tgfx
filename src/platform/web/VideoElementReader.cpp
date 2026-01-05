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

#include "tgfx/platform/web/VideoElementReader.h"
#include "platform/web/VideoElement.h"

namespace tgfx {
using namespace emscripten;

std::shared_ptr<VideoElementReader> VideoElementReader::MakeFrom(val video, int width, int height) {
  if (video == val::null() || width < 1 || height < 1) {
    return nullptr;
  }
  auto imageStream = VideoElement::MakeFrom(video, width, height);
  if (imageStream == nullptr) {
    return nullptr;
  }
  auto imageReader =
      std::shared_ptr<VideoElementReader>(new VideoElementReader(std::move(imageStream)));
  imageReader->weakThis = imageReader;
  return imageReader;
}

VideoElementReader::VideoElementReader(std::shared_ptr<ImageStream> stream)
    : ImageReader(std::move(stream)) {
}
}  // namespace tgfx
