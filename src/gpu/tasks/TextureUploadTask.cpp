/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "TextureUploadTask.h"
#include <algorithm>
#include "gpu/resources/TextureView.h"
#include "inspect/InspectorMark.h"
#include "tgfx/core/Clock.h"

namespace tgfx {
// Per-frame profiling accumulator. Lives in file scope because TextureUploadTask instances are
// short-lived (one per upload). The single-threaded WeChat Mini Program WASM environment makes a
// plain static variable safe; if this ever runs on a multi-threaded build, the accumulator would
// need to be promoted to a thread_local or atomic structure.
static TextureUploadTask::ProfileSnapshot gProfileSnapshot = {};

TextureUploadTask::ProfileSnapshot TextureUploadTask::FetchProfileAndReset() {
  auto snapshot = gProfileSnapshot;
  gProfileSnapshot = {};
  return snapshot;
}

TextureUploadTask::TextureUploadTask(std::shared_ptr<ResourceProxy> proxy,
                                     std::shared_ptr<DataSource<ImageBuffer>> source,
                                     bool mipmapped, int expectedWidth, int expectedHeight,
                                     bool alphaOnly)
    : ResourceTask(std::move(proxy)), source(std::move(source)), mipmapped(mipmapped),
      expectedWidth(expectedWidth), expectedHeight(expectedHeight), alphaOnly(alphaOnly) {
}

std::shared_ptr<Resource> TextureUploadTask::onMakeResource(Context* context) {
  TASK_MARK(tgfx::inspect::OpTaskType::TextureUploadTask);
  ATTRIBUTE_NAME("mipmaped", mipmapped);
  if (source == nullptr) {
    return nullptr;
  }
  int64_t getDataStartUs = Clock::Now();
  auto imageBuffer = source->getData();
  int64_t getDataEndUs = Clock::Now();
  if (imageBuffer == nullptr) {
    LOGE("TextureUploadTask::onMakeResource() Failed to decode the image!");
    return nullptr;
  }
  int32_t width = imageBuffer->width();
  int32_t height = imageBuffer->height();
  // Approximate upload payload: 1 byte/pixel for alpha-only, 4 bytes/pixel otherwise. Mipmaps add
  // roughly another 33% but are ignored here; use the widened estimate in interpretation.
  int bytesPerPixel = imageBuffer->isAlphaOnly() ? 1 : 4;
  uint64_t bytes = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) *
                   static_cast<uint64_t>(bytesPerPixel);

  int64_t makeTextureStartUs = Clock::Now();
  auto textureView = TextureView::MakeFrom(context, imageBuffer, mipmapped);
  int64_t makeTextureEndUs = Clock::Now();

  int64_t getDataUs = getDataEndUs - getDataStartUs;
  int64_t makeTextureUs = makeTextureEndUs - makeTextureStartUs;

  gProfileSnapshot.getDataUs += getDataUs;
  gProfileSnapshot.makeTextureUs += makeTextureUs;
  gProfileSnapshot.uploadedBytes += bytes;
  gProfileSnapshot.maxPixelWidth = std::max(gProfileSnapshot.maxPixelWidth, width);
  gProfileSnapshot.maxPixelHeight = std::max(gProfileSnapshot.maxPixelHeight, height);
  gProfileSnapshot.count++;

  // Per-upload diagnostics: emit one line for any upload whose getData()+MakeFrom() exceeds the
  // threshold. Comparing decoded (width,height) with the expected (expectedWidth,expectedHeight)
  // tells us whether decoding ran at full resolution or at a reduced ScaledImageGenerator size.
  // A zero getData() time means the ImageBuffer was preloaded (e.g. async-decoded or wrapped from
  // a PixelBuffer), so the cost is pure GPU upload.
  constexpr int64_t SLOW_UPLOAD_THRESHOLD_US = 20 * 1000;  // 20ms
  if (getDataUs + makeTextureUs > SLOW_UPLOAD_THRESHOLD_US) {
    const char* decodeShape =
        (expectedWidth > 0 && (width != expectedWidth || height != expectedHeight)) ? "scaled"
                                                                                    : "full";
    LOGI(
        "[TextureUpload] slow: decoded=%dx%d expected=%dx%d shape=%s alphaOnly=%d mipmap=%d "
        "decode=%.2fms upload=%.2fms",
        width, height, expectedWidth, expectedHeight, decodeShape, alphaOnly ? 1 : 0,
        mipmapped ? 1 : 0, static_cast<double>(getDataUs) / 1000.0,
        static_cast<double>(makeTextureUs) / 1000.0);
  }

  if (textureView == nullptr) {
    LOGE("TextureUploadTask::onMakeResource() Failed to upload the texture view!");
  } else {
    // Free the image source immediately to reduce memory pressure.
    source = nullptr;
  }
  return textureView;
}
}  // namespace tgfx
