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
//  either express or implied. See the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>

namespace tgfx {

/**
 * The semantic name of one value a chain program reads: the plan registers a member per value
 * (the reader that fills it), the writer fills it per draw, and the operator table names it as
 * an argument source.
 */
enum class ChainBindingSource : uint8_t {
  // The per-slot typed parameters of the pointwise operators.
  SlotColorMatrixLinear,
  SlotColorMatrixTranslate,
  SlotLumaCoefficients,
  SlotAlphaThreshold,
  SlotConstColor,
  SlotAARect,
  SlotLocalRect,
  SlotLocalRectMatrix,
  SlotRRect,
  SlotRRectRadiiX,
  SlotRRectRadiiY,
  SlotRRectAntiAlias,
  SlotRRectMatrix,
  // The geometry-facing switches.
  GeometryColor,
  GeometryHasCommonColor,
  GeometryStroke,
  GeometryAA,
  GeometryCoverage,
  // The output formula.
  OutputSwizzle,
  XpBlendMode,
  DstTextureUpperLeft,
  DstTextureCoordScale,
  // The stored vertex stage's block.
  VertexMatrix,
  VertexTexSubsetMatrix,
  RTAdjust,
  CoordTransform,
  // The gradient source's named block.
  GradientLayoutType,
  GradientColorizerKind,
  GradientLutLeaf,
  GradientLeftBorder,
  GradientRightBorder,
  GradientStart,
  GradientEnd,
  GradientScale01,
  GradientBias01,
  GradientScale23,
  GradientBias23,
  GradientThreshold,
  GradientBias,
  GradientScale,
  GradientThresholds1_7,
  GradientThresholds9_13,
  GradientScales,
  GradientBiases,
  // The noise source.
  NoiseFrequencyStitch,
  NoiseModeOctaves,
  // The color-space transform's block.
  CSFlags,
  CSTransferSrc0,
  CSTransferSrc1,
  CSTransferSrcType,
  CSOotfSrc,
  CSColorXform,
  CSOotfDst,
  CSTransferDst0,
  CSTransferDst1,
  CSTransferDstType,
  // The sampler children.
  LeafSubset,
  LeafAlphaStart,
  LeafHasRgbaaa,
  LeafSubsetClampOn,
  LeafReadSwizzles,
  DeviceCoordMatrix,
  HasMaskTexture,
  DeviceMaskSubset,
  DeviceLeafMask,
  DeviceLeafCoordMatrix,
};

}  // namespace tgfx
