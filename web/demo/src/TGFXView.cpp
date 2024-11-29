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

#include "TGFXView.h"

namespace hello2d {

TGFXView::TGFXView(const std::string& canvasID) : TGFXBaseView(canvasID) {
  // To utilize a custom typeface, you can load it directly into the browser and access it using
  // tgfx::Typeface::MakeFromName().
  auto typeface = tgfx::Typeface::MakeFromName("Arial", "Regular");
  if (typeface) {
    appHost->addTypeface("default", std::move(typeface));
  }
  typeface = tgfx::Typeface::MakeFromName("Noto Color Emoji", "Regular");
  if (typeface) {
    appHost->addTypeface("emoji", std::move(typeface));
  }
}

}  // namespace hello2d