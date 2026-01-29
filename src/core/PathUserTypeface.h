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

#include "UserTypeface.h"
#include "tgfx/core/CustomTypeface.h"

namespace tgfx {
class PathUserTypeface final : public UserTypeface {
 public:
  using VectorProviderType = std::vector<std::shared_ptr<PathProvider>>;

  static std::shared_ptr<UserTypeface> Make(uint32_t builderID, const std::string& fontFamily,
                                            const std::string& fontStyle,
                                            const FontMetrics& fontMetrics, const Rect& fontBounds,
                                            float unitsPerEm,
                                            const VectorProviderType& glyphPathProviders);
  size_t glyphsCount() const override;

  bool hasColor() const override;

  bool hasOutlines() const override;

  std::shared_ptr<PathProvider> getPathProvider(GlyphID glyphID) const;

  std::shared_ptr<ScalerContext> onCreateScalerContext(float size) const override;

 private:
  explicit PathUserTypeface(uint32_t builderID, const std::string& fontFamily,
                            const std::string& fontStyle, const FontMetrics& fontMetrics,
                            const Rect& fontBounds, float unitsPerEm,
                            const VectorProviderType& glyphPathProviders);

  VectorProviderType glyphPathProviders = {};
};
}  // namespace tgfx
