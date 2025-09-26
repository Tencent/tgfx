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
#include "core/utils/Types.h"
#include "tgfx/core/ColorFilter.h"

namespace tgfx {
/**
 * LumaFilter cannot be replaced by MatrixFilter because MatrixFilter uses non-premultiplied RGBA,
 * while LumaFilter uses premultiplied RGBA.
 */
class LumaColorFilter : public ColorFilter {
 protected:
  Type type() const override {
    return Type::Luma;
  }

  bool isEqual(const ColorFilter* colorFilter) const override {
    auto type = Types::Get(colorFilter);
    return type == Type::Luma;
  }

  PlacementPtr<FragmentProcessor> asFragmentProcessor(
      Context* context, std::shared_ptr<ColorSpace> dstColorSpace) const override;
};
}  // namespace tgfx
