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
#include "tgfx/gpu/RenderPass.h"
#include "tgfx/gpu/CompareFunction.h"
#include "tgfx/gpu/StencilOperation.h"
#include "tgfx/gpu/BlendFactor.h"
#include "tgfx/gpu/BlendOperation.h"
#include "tgfx/gpu/Sampler.h"
#include "tgfx/gpu/FilterMode.h"
#include "tgfx/gpu/MipmapMode.h"

namespace tgfx {

/**
 * Metal-specific constants and utility definitions.
 */
class MetalDefines {
 public:
  /**
   * Convert TGFX texture usage to Metal texture usage.
   */
  static MTLTextureUsage ToMTLTextureUsage(uint32_t usage);

  /**
   * Convert TGFX buffer usage to Metal resource options.
   */
  static MTLResourceOptions ToMTLResourceOptions(uint32_t usage);

  /**
   * Convert TGFX primitive type to Metal primitive type.
   */
  static MTLPrimitiveType ToMTLPrimitiveType(PrimitiveType primitiveType);

  /**
   * Convert TGFX vertex format to Metal vertex format.
   */
  static MTLVertexFormat ToMTLVertexFormat(VertexFormat format);

  /**
   * Convert TGFX compare function to Metal compare function.
   */
  static MTLCompareFunction ToMTLCompareFunction(CompareFunction compareFunction);

  /**
   * Convert TGFX stencil operation to Metal stencil operation.
   */
  static MTLStencilOperation ToMTLStencilOperation(StencilOperation stencilOp);

  /**
   * Convert TGFX blend factor to Metal blend factor.
   */
  static MTLBlendFactor ToMTLBlendFactor(BlendFactor blendFactor);

  /**
   * Convert TGFX blend operation to Metal blend operation.
   */
  static MTLBlendOperation ToMTLBlendOperation(BlendOperation blendOp);

  /**
   * Convert TGFX sampler address mode to Metal sampler address mode.
   */
  static MTLSamplerAddressMode ToMTLSamplerAddressMode(AddressMode addressMode);

  /**
   * Convert TGFX sampler filter to Metal sampler filter.
   */
  static MTLSamplerMinMagFilter ToMTLSamplerFilter(FilterMode filter);

  /**
   * Convert TGFX sampler mipmap filter to Metal sampler mipmap filter.
   */
  static MTLSamplerMipFilter ToMTLSamplerMipFilter(MipmapMode filter);

  /**
   * Get the bytes per pixel for a Metal pixel format.
   */
  static size_t GetBytesPerPixel(MTLPixelFormat format);


  /**
   * Convert TGFX cull mode to Metal cull mode.
   */
  static MTLCullMode ToMTLCullMode(CullMode cullMode);

  /**
   * Convert TGFX front face to Metal winding order.
   */
  static MTLWinding ToMTLWinding(FrontFace frontFace);
};

}  // namespace tgfx