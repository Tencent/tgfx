/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "pdf/PDFTypes.h"
#include "tgfx/core/Brush.h"

namespace tgfx {

#pragma pack(push, 1)
struct PDFFillGraphicState {
  float alpha;
  uint8_t blendMode;
  uint8_t PADDING[3] = {0, 0, 0};

  bool operator==(const PDFFillGraphicState& other) const {
    return alpha == other.alpha && blendMode == other.blendMode && PADDING[0] == other.PADDING[0] &&
           PADDING[1] == other.PADDING[1] && PADDING[2] == other.PADDING[2];
  }

  bool operator!=(const PDFFillGraphicState& o) const {
    return !(*this == o);
  }

  std::size_t hash() const {
    std::size_t h1 = std::hash<float>{}(alpha);
    std::size_t h2 = std::hash<uint8_t>{}(blendMode);
    std::size_t h3 = std::hash<uint8_t>{}(PADDING[0]);
    std::size_t h4 = std::hash<uint8_t>{}(PADDING[1]);
    std::size_t h5 = std::hash<uint8_t>{}(PADDING[2]);
    return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3) ^ (h5 << 4);
  }
};
#pragma pack(pop)

class PDFGraphicState {
 public:
  enum class SMaskMode {
    Alpha,
    Luminosity,
  };

  static PDFIndirectReference GetGraphicStateForPaint(PDFDocumentImpl* document,
                                                      const Brush& brush);

  static PDFIndirectReference GetSMaskGraphicState(PDFIndirectReference sMask, bool invert,
                                                   SMaskMode sMaskMode, PDFDocumentImpl* document);
};

}  // namespace tgfx

namespace std {
template <>
struct hash<tgfx::PDFFillGraphicState> {
  std::size_t operator()(const tgfx::PDFFillGraphicState& s) const {
    return s.hash();
  }
};
}  // namespace std