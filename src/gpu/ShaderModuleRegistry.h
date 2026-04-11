/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

namespace tgfx {

enum class ShaderModuleID {
  TypesGLSL,
  ConstColor,
  LinearGradientLayout,
  SingleIntervalGradientColorizer,
  // Phase 2 batch 1
  RadialGradientLayout,
  DiamondGradientLayout,
  ConicGradientLayout,
  AARectEffect,
  ColorMatrix,
  Luma,
  // Phase 2 batch 2
  DualIntervalGradientColorizer,
  AlphaThreshold,
  TextureGradientColorizer,
  DeviceSpaceTextureEffect,
  // Complex leaf FPs
  TextureEffect,
  TiledTextureEffect,
  UnrolledBinaryGradientColorizer,
  // XferProcessor modules
  BlendModes,
  EmptyXfer,
  PorterDuffXfer,
  // Container FP modules
  XfermodeEffect,
  ClampedGradientEffect,
  // Reclassified leaf FP (was container)
  ColorSpaceXformEffect,
};

/**
 * Registry of embedded shader module source code. Module text is compiled into the binary as C++
 * string constants — no runtime file I/O is needed.
 */
class ShaderModuleRegistry {
 public:
  /**
   * Returns the GLSL source text for the given module.
   */
  static const std::string& GetModule(ShaderModuleID id);

  /**
   * Returns true if the given processor name has a registered modular shader.
   */
  static bool HasModule(const std::string& processorName);

  /**
   * Returns the module ID for a processor name. The processor must have a registered module
   * (check with HasModule() first).
   */
  static ShaderModuleID GetModuleID(const std::string& processorName);
};

}  // namespace tgfx
