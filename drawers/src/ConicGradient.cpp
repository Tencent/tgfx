/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "base/Drawers.h"
#include "tgfx/core/ColorSpace.h"

namespace drawers {
void ConicGradient::onDraw(tgfx::Canvas* canvas, const AppHost* host) {
    (void)host;
    tgfx::Color green = tgfx::Color::FromRGBAWithCS(0, 255, 0, 255,
                                                    tgfx::ColorSpace::MakeRGB(
                                                            tgfx::namedTransferFn::SRGB,
                                                            tgfx::namedGamut::DisplayP3));
    canvas->drawColor(green);
}

}  // namespace drawers
