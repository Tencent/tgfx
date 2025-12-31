/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making libpag available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <webgpu/webgpu_cpp.h>
#include "tgfx/gpu/Attribute.h"
#include "tgfx/gpu/BlendFactor.h"
#include "tgfx/gpu/BlendOperation.h"
#include "tgfx/gpu/CompareFunction.h"
#include "tgfx/gpu/PixelFormat.h"
#include "tgfx/gpu/RenderPipeline.h"
#include "tgfx/gpu/Sampler.h"
#include "tgfx/gpu/StencilOperation.h"

namespace tgfx {

wgpu::TextureFormat ToWGPUTextureFormat(PixelFormat pixelFormat);

wgpu::TextureUsage ToWGPUTextureUsage(uint32_t usage);

wgpu::AddressMode ToWGPUAddressMode(AddressMode mode);

wgpu::FilterMode ToWGPUFilterMode(FilterMode mode);

wgpu::MipmapFilterMode ToWGPUMipmapFilterMode(MipmapMode mode);

wgpu::VertexFormat ToWGPUVertexFormat(VertexFormat format);

wgpu::BlendFactor ToWGPUBlendFactor(BlendFactor factor);

wgpu::BlendOperation ToWGPUBlendOperation(BlendOperation op);

wgpu::CompareFunction ToWGPUCompareFunction(CompareFunction func);

wgpu::StencilOperation ToWGPUStencilOperation(StencilOperation op);

wgpu::CullMode ToWGPUCullMode(CullMode mode);

wgpu::FrontFace ToWGPUFrontFace(FrontFace face);

}  // namespace tgfx
