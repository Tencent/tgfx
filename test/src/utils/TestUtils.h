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

#pragma once

#include "base/TGFXTest.h"
#include "core/PixelBuffer.h"
#include "gtest/gtest.h"
#include "tgfx/core/Clock.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/ImageCodec.h"
#include "tgfx/core/Pixmap.h"
#include "utils/Baseline.h"
#include "utils/ContextScope.h"
#include "utils/DevicePool.h"
#include "utils/ProjectPath.h"

namespace tgfx {

unsigned CreateGLProgram(Context* context, const std::string& vertex, const std::string& fragment);

bool CreateGLTexture(Context* context, int width, int height, GLTextureInfo* texture);

std::shared_ptr<ImageCodec> MakeImageCodec(const std::string& path);

std::shared_ptr<ImageCodec> MakeNativeCodec(const std::string& path);

std::shared_ptr<Image> MakeImage(const std::string& path);

std::shared_ptr<Typeface> MakeTypeface(const std::string& path);

std::shared_ptr<Data> ReadFile(const std::string& path);

void SaveFile(std::shared_ptr<Data> data, const std::string& key);

void SaveWebpFile(std::shared_ptr<Data> data, const std::string& key);

void SaveImage(std::shared_ptr<PixelBuffer> pixelBuffer, const std::string& key,
               std::shared_ptr<ColorSpace> colorSpace = ColorSpace::MakeSRGB());

void SaveImage(const Bitmap& bitmap, const std::string& key,
               std::shared_ptr<ColorSpace> colorSpace = ColorSpace::MakeSRGB());

void SaveImage(const Pixmap& pixmap, const std::string& key,
               std::shared_ptr<ColorSpace> colorSpace = ColorSpace::MakeSRGB());

void RemoveImage(const std::string& key);

void RemoveFile(const std::string& key);

std::shared_ptr<Image> ScaleImage(const std::shared_ptr<Image>& image, float scale,
                                  const SamplingOptions& options = {});

}  // namespace tgfx
