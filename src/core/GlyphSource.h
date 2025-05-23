/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "core/DataSource.h"
#include "core/PixelBuffer.h"
#include "tgfx/core/ImageCodec.h"

namespace tgfx {
class GlyphSource : public DataSource<PixelBuffer> {
 public:
  static std::unique_ptr<DataSource> MakeFrom(std::shared_ptr<ImageCodec> imageCodec,
                                              bool tryHardware = true, bool asyncDecoding = true);

  std::shared_ptr<PixelBuffer> getData() const override;

  GlyphSource(std::shared_ptr<ImageCodec> imageCodec, bool tryHardware = true);

 private:
  std::shared_ptr<ImageCodec> imageCodec = nullptr;
  bool tryHardware = true;
};
}  // namespace tgfx
