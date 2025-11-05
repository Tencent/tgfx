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
#include <src/skcms_public.h>
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/YUVColorSpace.h"

namespace tgfx {
std::shared_ptr<ColorSpace> MakeColorSpaceFromYUVColorSpace(YUVColorSpace yuvColorSpace);

std::shared_ptr<ColorSpace> AndroidDataSpaceToColorSpace(int standard, int transfer);

gfx::skcms_ICCProfile ToSkcmsICCProfile(std::shared_ptr<ColorSpace> colorSpace);

bool ColorSpaceIsEqual(std::shared_ptr<ColorSpace> src, std::shared_ptr<ColorSpace> dst);
}  // namespace tgfx
