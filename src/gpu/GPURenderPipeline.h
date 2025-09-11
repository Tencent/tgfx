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

#include <string>
#include <vector>
#include "gpu/Attribute.h"
#include "gpu/BlendFactor.h"
#include "gpu/BlendOperation.h"
#include "gpu/GPUResource.h"
#include "gpu/GPUShaderModule.h"
#include "gpu/Uniform.h"
#include "tgfx/gpu/PixelFormat.h"

namespace tgfx {
/**
 * Values used to specify a mask to permit or restrict writing to color channels of a color value.
 */
class ColorWriteMask {
 public:
  /**
   * The red color channel is enabled.
   */
  static constexpr uint32_t RED = 0x1;

  /**
   * The green color channel is enabled.
   */
  static constexpr uint32_t GREEN = 0x2;

  /**
   * The blue color channel is enabled.
   */
  static constexpr uint32_t BLUE = 0x4;

  /**
   * The alpha color channel is enabled.
   */
  static constexpr uint32_t ALPHA = 0x8;

  /**
   * All color channels are enabled.
   */
  static constexpr uint32_t All = 0xF;
};

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
   * A GPUShaderModule object containing the fragment shader code.
   */
  GPUShaderModule* module = nullptr;

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
   * A GPUShaderModule object containing the vertex shader code.
   */
  GPUShaderModule* module = nullptr;

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
   * Creates an empty BindingEntry.
   */
  BindingEntry() = default;

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

  /**
   * Lists the uniform variables contained in a uniform block. This is only used when UBOs are not
   * supported.
   */
  std::vector<Uniform> uniforms = {};
};

/**
 * BindingLayout describes the layout of resources (uniform blocks and texture samplers) used by
 * a shader program in a rendering pipeline.
 */
class BindingLayout {
 public:
  /**
   * Creates an empty BindingLayout.
   */
  BindingLayout() = default;

  /**
   * Creates a BindingLayout with the specified uniform blocks and texture samplers.
   */
  BindingLayout(std::vector<BindingEntry> uniformBlocks, std::vector<BindingEntry> textureSamplers)
      : uniformBlocks(std::move(uniformBlocks)), textureSamplers(std::move(textureSamplers)) {
  }

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
 * Options you provide to a GPU device to create a render pipeline state.
 */
class GPURenderPipelineDescriptor {
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
};

/**
 * GPURenderPipeline represents a graphics pipeline configuration for a render pass, which the pass
 * applies to the draw commands you encode.
 */
class GPURenderPipeline : public GPUResource {
 public:
};
}  // namespace tgfx
