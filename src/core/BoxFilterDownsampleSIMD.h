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
#include <cstdint>
namespace tgfx {
int ResizeAreaFastx2SIMDFunc(int channelNum, int srcStep, int dstStep, const uint8_t* srcData,
                             uint8_t* dstData, int w, int, int padding, int shiftNum);

int ResizeAreaFastx4SIMDFunc(int channelNum, int srcStep, int dstStep, const uint8_t* srcData,
                             uint8_t* dstData, int w, int scale, int padding, int shiftNum);

int ResizeAreaFastx8SIMDFunc(int channelNum, int srcStep, int dstStep, const uint8_t* srcData,
                             uint8_t* dstData, int w, int scale, int padding, int shiftNum);

int ResizeAreaFastx16SimdFunc(int channelNum, int srcStep, int dstStep, const uint8_t* srcData,
                              uint8_t* dstData, int w, int scale, int padding, int shiftNum);

int ResizeAreaFastxNSimdFunc(int channelNum, int srcStep, int dstStep, const uint8_t* srcData,
                             uint8_t* dstData, int w, int scale, int padding, int shiftNum);

void Mul(const float* buf, int width, float beta, float* sum);

void MulAdd(const float* buf, int width, float beta, float* sum);
}  // namespace tgfx
