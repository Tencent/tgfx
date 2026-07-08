/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include <webgpu/webgpu.h>
#include "tgfx/gpu/Attribute.h"
#include "tgfx/gpu/BlendFactor.h"
#include "tgfx/gpu/BlendOperation.h"
#include "tgfx/gpu/CompareFunction.h"
#include "tgfx/gpu/FilterMode.h"
#include "tgfx/gpu/MipmapMode.h"
#include "tgfx/gpu/RenderPass.h"
#include "tgfx/gpu/Sampler.h"
#include "tgfx/gpu/StencilOperation.h"

namespace tgfx {

WGPUTextureUsageFlags ToWGPUTextureUsage(uint32_t usage);

WGPUBufferUsageFlags ToWGPUBufferUsage(uint32_t usage);

WGPUPrimitiveTopology ToWGPUPrimitiveTopology(PrimitiveType primitiveType);

WGPUVertexFormat ToWGPUVertexFormat(VertexFormat format);

WGPUCompareFunction ToWGPUCompareFunction(CompareFunction compareFunction);

WGPUStencilOperation ToWGPUStencilOperation(StencilOperation stencilOp);

WGPUBlendFactor ToWGPUBlendFactor(BlendFactor blendFactor);

WGPUBlendOperation ToWGPUBlendOperation(BlendOperation blendOp);

WGPUAddressMode ToWGPUAddressMode(AddressMode addressMode);

WGPUFilterMode ToWGPUFilterMode(FilterMode filter);

WGPUMipmapFilterMode ToWGPUMipmapFilterMode(MipmapMode filter);

size_t WGPUTextureFormatBytesPerPixel(WGPUTextureFormat format);

WGPUTextureFormat ToWGPUTextureFormat(PixelFormat format);

WGPULoadOp ToWGPULoadOp(LoadAction loadAction);

WGPUStoreOp ToWGPUStoreOp(StoreAction storeAction);

WGPUCullMode ToWGPUCullMode(CullMode cullMode);

WGPUFrontFace ToWGPUFrontFace(FrontFace frontFace);

WGPUIndexFormat ToWGPUIndexFormat(IndexFormat format);

WGPUColorWriteMaskFlags ToWGPUColorWriteMask(uint32_t mask);

}  // namespace tgfx
