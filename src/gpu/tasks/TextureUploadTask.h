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

#pragma once

#include <cstdint>
#include "ResourceTask.h"
#include "core/ImageSource.h"

namespace tgfx {
class TextureUploadTask : public ResourceTask {
 public:
  /**
   * Per-frame profiling counters accumulated by TextureUploadTask. Values are fetched and reset
   * via FetchProfileAndReset() at the end of each DrawingBuffer::encode() when a slow frame is
   * detected, so the counters always describe a single frame's work.
   */
  struct ProfileSnapshot {
    int64_t getDataUs = 0;       // Total time spent decoding image data to an ImageBuffer.
    int64_t makeTextureUs = 0;   // Total time spent uploading to GPU textures.
    uint64_t uploadedBytes = 0;  // Approximate bytes uploaded (w * h * bytesPerPixel).
    int32_t maxPixelWidth = 0;   // Widest single image seen this frame.
    int32_t maxPixelHeight = 0;  // Tallest single image seen this frame.
    uint32_t count = 0;          // Number of uploads successfully measured.
  };

  /**
   * Returns the accumulated profiling counters and resets them to zero. Intended to be called
   * from DrawingBuffer::encode() when emitting slow-frame diagnostics.
   */
  static ProfileSnapshot FetchProfileAndReset();

  TextureUploadTask(std::shared_ptr<ResourceProxy> proxy,
                    std::shared_ptr<DataSource<ImageBuffer>> source, bool mipmapped,
                    int expectedWidth, int expectedHeight, bool alphaOnly);

  ResourceTaskType type() const override {
    return ResourceTaskType::Texture;
  }

  std::shared_ptr<Resource> onMakeResource(Context* context) override;

 private:
  std::shared_ptr<DataSource<ImageBuffer>> source = nullptr;
  bool mipmapped = false;
  // Expected output dimensions and channel layout captured at task-creation time. Comparing these
  // against the decoded ImageBuffer's actual dimensions in onMakeResource() lets slow-frame
  // diagnostics tell whether the upload was served by a plain codec (sizes match) or by a
  // ScaledImageGenerator-wrapped codec (decoded size strictly smaller than expected).
  int expectedWidth = 0;
  int expectedHeight = 0;
  bool alphaOnly = false;
};
}  // namespace tgfx
