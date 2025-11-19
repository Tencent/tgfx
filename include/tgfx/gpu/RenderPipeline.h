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
#include <string>
#include <vector>
#include "tgfx/gpu/Attribute.h"
#include "tgfx/gpu/BlendFactor.h"
#include "tgfx/gpu/BlendOperation.h"
#include "tgfx/gpu/ColorWriteMask.h"
#include "tgfx/gpu/CompareFunction.h"
#include "tgfx/gpu/PixelFormat.h"
#include "tgfx/gpu/ShaderModule.h"
#include "tgfx/gpu/StencilOperation.h"

namespace tgfx {
/**
* PipelineColorAttachment specifies the color format and blending settings for an individual color
* attachment within a rendering pipeline.
 */
class PipelineColorAttachment {
 public:
  /**
   * The pixel format of the color attachment’s texture.
   */
  PixelFormat format = PixelFormat::RGBA_8888;

  /**
   * Determines whether blending is enabled for this color attachment. If blending is disabled, the
   * fragment's color is passed through unchanged.
   */
  bool blendEnable = false;

  /**
   * Determines which blend factor is used to determine the source factors (Sr,Sg,Sb).
   */
  BlendFactor srcColorBlendFactor = BlendFactor::One;

  /**
   * Determines which blend factor is used to determine the destination factors (Dr,Dg,Db).
   */
  BlendFactor dstColorBlendFactor = BlendFactor::Zero;

  /**
   * Determines which blend operation is used to calculate the RGB values to write to the color
   * attachment.
   */
  BlendOperation colorBlendOp = BlendOperation::Add;

  /**
   * Determines which blend factor is used to determine the source alpha factor (Sa).
   */
  BlendFactor srcAlphaBlendFactor = BlendFactor::One;

  /**
   * Determines which blend factor is used to determine the destination alpha factor (Da).
   */
  BlendFactor dstAlphaBlendFactor = BlendFactor::Zero;

  /**
   * Determines which blend operation is used to calculate the alpha value to write to the color
   * attachment.
   */
  BlendOperation alphaBlendOp = BlendOperation::Add;

  /**
   * A bitmask that controls which color channels are written to the texture. See ColorWriteMask for
   * definitions.
   */
  uint32_t colorWriteMask = ColorWriteMask::All;
};

/**
 * FragmentDescriptor describes the fragment shader entry point and its output color attachments for
 * the pipeline.
 */
class FragmentDescriptor {
 public:
  /**
   * A ShaderModule object containing the fragment shader code.
   */
  std::shared_ptr<ShaderModule> module = nullptr;

  /**
   * The name of the entry point function in the shader code.
   */
  std::string entryPoint = "main";

  /**
   * An array of PipelineColorAttachment objects that define the color attachments for the render
   * pipeline.
   */
  std::vector<PipelineColorAttachment> colorAttachments = {};
};

/**
 * VertexDescriptor describes the vertex shader entry point and the input buffer layouts for the
 * pipeline.
 */
class VertexDescriptor {
 public:
  /**
   * Creates an empty vertex descriptor.
   */
  VertexDescriptor() = default;

  /**
   * Creates a vertex descriptor with the specified attributes and vertex stride.
   * If vertexStride is 0, it will be calculated as the sum of the sizes of all attributes.
   */
  VertexDescriptor(std::vector<Attribute> attributes, size_t vertexStride = 0);

  /**
   * A ShaderModule object containing the vertex shader code.
   */
  std::shared_ptr<ShaderModule> module = nullptr;

  /**
   * The name of the entry point function in the shader code.
   */
  std::string entryPoint = "main";

  /**
   * An array of state data that describes how vertex attribute data is stored in memory and is
   * mapped to arguments for a vertex shader function.
   */
  std::vector<Attribute> attributes = {};

  /**
   * The number of bytes between the first byte of two consecutive vertices in a buffer.
   */
  size_t vertexStride = 0;
};

/**
 * BindingEntry describes a resource binding in a shader program, such as a uniform block or a
 * texture sampler.
 */
class BindingEntry {
 public:
  /**
   * Creates a BindingEntry with the specified name and binding point.
   */
  BindingEntry(std::string name, unsigned binding) : name(std::move(name)), binding(binding) {
  }

  /**
   * The name of the resource in the shader program.
   */
  std::string name;

  /**
   * The binding point of the resource.
   */
  unsigned binding = 0;
};

/**
 * BindingLayout describes the layout of resources (uniform blocks and texture samplers) used by
 * a shader program in a rendering pipeline.
 */
class BindingLayout {
 public:
  /**
   * Specifies the binding points for uniform blocks used in the shader program.
   */
  std::vector<BindingEntry> uniformBlocks = {};

  /**
   * Specifies the binding points for texture samplers used in the shader program.
   */
  std::vector<BindingEntry> textureSamplers = {};
};

/**
 * An object that defines the front-facing or back-facing stencil operations of a depth and stencil
 * state object.
 */
class StencilDescriptor {
 public:
  /**
   * The function used to compare the existing stencil value in the buffer with the reference value.
   */
  CompareFunction compare = CompareFunction::Always;

  /**
   * The operation to perform on the stencil buffer when the depth comparison test fails.
   */
  StencilOperation depthFailOp = StencilOperation::Keep;

  /**
   * The operation to perform on the stencil buffer when the stencil comparison test fails.
   */
  StencilOperation failOp = StencilOperation::Keep;

  /**
   * The operation to perform on the stencil buffer when both the depth and stencil comparison tests
   * pass.
   */
  StencilOperation passOp = StencilOperation::Keep;
};

/**
 * DepthStencilDescriptor describes the depth and stencil state for a render pipeline.
 */
class DepthStencilDescriptor {
 public:
  /**
   * A comparison function used to test fragment depths against depthStencilAttachment depth values.
   */
  CompareFunction depthCompare = CompareFunction::Always;

  /**
   * A Boolean value that indicates whether depth values can be written to the depth attachment.
   */
  bool depthWriteEnabled = false;

  /**
   * An object that defines the front-facing stencil operations.
   */
  StencilDescriptor stencilBack = {};

  /**
   * An object that defines the back-facing stencil operations.
   */
  StencilDescriptor stencilFront = {};

  /**
    * A bitmask that determines from which bits that stencil comparison tests can read.
   */
  uint32_t stencilReadMask = 0xFFFFFFFF;

  /**
   * A bitmask that determines to which bits that stencil operations can write.
   */
  uint32_t stencilWriteMask = 0xFFFFFFFF;
};

/**
 * The winding order that determines which polygons are considered front-facing.
 */
enum class FrontFaceDirection {
  /**
   * The front face vertex order is clockwise
   */
  CW,
  /**
   * The front face vertex order is counterclockwise
   */
  CCW
};

/**
 * The culling mode: specifies whether to cull front faces, back faces, or both.
 */
enum class CullFaceMode {
  /**
   * Cull front faces
   */
  Front,
  /**
   * Cull back faces
   */
  Back,
  /**
   * Cull both front and back faces
   */
  FrontAndBack
};

/**
 * PrimitiveDescriptor defines the face culling configuration for a render pipeline.
 */
class PrimitiveDescriptor {
 public:
  /**
   * Enables or disables face culling.
   */
  bool enabled = false;

  /**
   * The winding order used to identify front-facing polygons.
   */
  FrontFaceDirection frontDirection = FrontFaceDirection::CCW;

  /**
   * The culling mode: determines which faces (front, back, or both) are culled.
   */
  CullFaceMode mode = CullFaceMode::Back;
};

/**
 * Options you provide to a GPU device to create a render pipeline state.
 */
class RenderPipelineDescriptor {
 public:
  /**
   * The vertex shader entry point and its input buffer layouts.
   */
  VertexDescriptor vertex = {};

  /**
   * The fragment shader entry point and its output color attachments.
   */
  FragmentDescriptor fragment = {};

  /**
   * Specifies the layout of resources (uniform blocks and texture samplers) used by the shader
   * program in the rendering pipeline. This is optional if binding points are hardcoded in the
   * shader code.
   */
  BindingLayout layout = {};

  /**
   * An object that describes the depth and stencil state for the render pipeline.
   */
  DepthStencilDescriptor depthStencil = {};

  /**
   * An object that describes the face culling configuration for the render pipeline.
   */
  PrimitiveDescriptor primitive = {};
};

/**
 * RenderPipeline represents a graphics pipeline configuration for a render pass, which the pass
 * applies to the draw commands you encode.
 */
class RenderPipeline {
 public:
  virtual ~RenderPipeline() = default;
};
}  // namespace tgfx
