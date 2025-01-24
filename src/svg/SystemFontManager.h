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

#include <cstddef>
#include <memory>
#include <string>
#include "tgfx/core/FontStyle.h"
#include "tgfx/core/Typeface.h"
#include "tgfx/svg/FontManager.h"

namespace tgfx {

class SystemFontManager : public FontManager {
 public:
  static std::shared_ptr<SystemFontManager> Make() {
    return std::shared_ptr<SystemFontManager>(new SystemFontManager);
  }

 protected:
  SystemFontManager() = default;

  std::shared_ptr<Typeface> onMatchTypeface(const std::string& familyName,
                                            FontStyle style) const override;

  std::shared_ptr<Typeface> onGetFallbackTypeface(const std::string& familyName, FontStyle style,
                                                  Unichar character) const override;
};

}  // namespace tgfx