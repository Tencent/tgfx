/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "core/GlyphRun.h"
#include "tgfx/core/TextBlob.h"

namespace tgfx {
/**
 * A simple implementation of TextBlob that uses a single GlyphRun.
 */
class SimpleTextBlob : public TextBlob {
 public:
  explicit SimpleTextBlob(GlyphRun glyphRun) : glyphRun(std::move(glyphRun)) {
  }

  Rect getBounds() const override;

 protected:
  size_t glyphRunCount() const override {
    return 1;
  }

  const GlyphRun* getGlyphRun(size_t i) const override {
    return i == 0 ? &glyphRun : nullptr;
  }

 private:
  GlyphRun glyphRun = {};

  friend class TextBlob;
};
}  // namespace tgfx
