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
#include <memory>
#include <utility>
#include "GPUBuffer.h"
#include "ProgramInfo.h"
#include "RenderPass.h"
#include "UniformData.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {
AddressMode ToAddressMode(TileMode tileMode);
std::pair<std::shared_ptr<GPUBuffer>, size_t> SetupUniformBuffer(const Context* context,
                                                                 UniformData* uniformData);
void SetUniformBuffer(RenderPass* renderPass, std::shared_ptr<GPUBuffer> buffer, size_t offset,
                      size_t size, unsigned bindingPoint);
void SetupTextures(RenderPass* renderPass, GPU* gpu, const ProgramInfo& programInfo);
}  // namespace tgfx
