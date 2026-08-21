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

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/SurfaceReadback.h"

namespace tgfx {

/**
 * An image whose pixels are still being read back asynchronously. The SVG text carries the token in
 * place of the image data URI until SVGExporter fills in dataUri. flipY and colorSpace record how
 * the arriving pixels must be interpreted.
 */
struct PendingImage {
  std::string token;
  std::shared_ptr<SurfaceReadback> readback;
  bool flipY = false;
  std::shared_ptr<ColorSpace> colorSpace = nullptr;
  /**
   * Filled in once the pixels arrive, when the readback is released as well. Stays empty if the
   * pixels never arrive.
   */
  std::string dataUri;

  static PendingImage Make(std::shared_ptr<SurfaceReadback> readback, size_t index, bool flipY,
                           std::shared_ptr<ColorSpace> colorSpace) {
    return {"__tgfx_svg_pending_img_" + std::to_string(index) + "__",
            std::move(readback),
            flipY,
            std::move(colorSpace),
            {}};
  }

  /**
   * Wraps a token as the Data object that call sites embed in place of a real data URI. The
   * trailing NUL matches the layout produced by AsDataUri().
   */
  static std::shared_ptr<Data> MakeTokenData(const std::string& token) {
    return Data::MakeWithCopy(token.c_str(), token.size() + 1);
  }
};

}  // namespace tgfx
