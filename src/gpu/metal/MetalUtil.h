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

#include <Metal/Metal.h>
#include "tgfx/gpu/Attribute.h"
#include "tgfx/gpu/BlendFactor.h"
#include "tgfx/gpu/BlendOperation.h"
#include "tgfx/gpu/CompareFunction.h"
#include "tgfx/gpu/FilterMode.h"
#include "tgfx/gpu/MipmapMode.h"
#include "tgfx/gpu/PixelFormat.h"
#include "tgfx/gpu/RenderPass.h"
#include "tgfx/gpu/Sampler.h"
#include "tgfx/gpu/StencilOperation.h"

namespace tgfx {

MTLTextureUsage ToMTLTextureUsage(uint32_t usage);

MTLResourceOptions ToMTLResourceOptions(uint32_t usage);

MTLPrimitiveType ToMTLPrimitiveType(PrimitiveType primitiveType);

MTLVertexFormat ToMTLVertexFormat(VertexFormat format);

MTLCompareFunction ToMTLCompareFunction(CompareFunction compareFunction);

MTLStencilOperation ToMTLStencilOperation(StencilOperation stencilOp);

MTLBlendFactor ToMTLBlendFactor(BlendFactor blendFactor);

MTLBlendOperation ToMTLBlendOperation(BlendOperation blendOp);

MTLSamplerAddressMode ToMTLSamplerAddressMode(AddressMode addressMode);

MTLSamplerMinMagFilter ToMTLSamplerFilter(FilterMode filter);

MTLSamplerMipFilter ToMTLSamplerMipFilter(MipmapMode filter);

size_t MTLPixelFormatBytesPerPixel(MTLPixelFormat format);

MTLLoadAction ToMTLLoadAction(LoadAction loadAction);

MTLStoreAction ToMTLStoreAction(StoreAction storeAction);

MTLCullMode ToMTLCullMode(CullMode cullMode);

MTLWinding ToMTLWinding(FrontFace frontFace);

PixelFormat MTLPixelFormatToPixelFormat(MTLPixelFormat metalFormat);

}  // namespace tgfx
