/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/ProgramInfo.h"
#include "gpu/ProgramWriter.h"
#include "gpu/Swizzle.h"
#include "gpu/UniformData.h"
#include "gpu/YUVColorConversion.h"
#include "gpu/aot/bind/Binder.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/ir/EffectGraph.h"
#include "gpu/aot/select/FamilyMatch.h"
#include "gpu/processors/AtlasTextGeometryProcessor.h"
#include "gpu/processors/ComplexEllipseGeometryProcessor.h"
#include "gpu/processors/ComplexNonAARRectGeometryProcessor.h"
#include "gpu/processors/DefaultGeometryProcessor.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/EllipseGeometryProcessor.h"
#include "gpu/processors/HairlineLineGeometryProcessor.h"
#include "gpu/processors/HairlineQuadGeometryProcessor.h"
#include "gpu/processors/MeshGeometryProcessor.h"
#include "gpu/processors/NonAARRectGeometryProcessor.h"
#include "gpu/processors/PorterDuffXferProcessor.h"
#include "gpu/processors/QuadPerEdgeAAGeometryProcessor.h"
#include "gpu/processors/RRectEffect.h"
#include "gpu/processors/RectEffect.h"
#include "gpu/processors/RoundStrokeRectGeometryProcessor.h"
#include "gpu/processors/ShapeInstancedGeometryProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/shaders/Families.h"
#include "gpu/shaders/ParamBuffer.h"
#include "gpu/shaders/contract/DrawFacts.h"
#include "gpu/shaders/modules/AnalyticClip.h"
#include "gpu/shaders/modules/AtlasTextGeometry.h"
#include "gpu/shaders/modules/BlurSourceGeometry.h"
#include "gpu/shaders/modules/ComplexEllipseGeometry.h"
#include "gpu/shaders/modules/ComplexRRectGeometry.h"
#include "gpu/shaders/modules/CoverageTexture.h"
#include "gpu/shaders/modules/DeviceMask.h"
#include "gpu/shaders/modules/DeviceMaskRuntime.h"
#include "gpu/shaders/modules/EllipseGeometry.h"
#include "gpu/shaders/modules/Frame.h"
#include "gpu/shaders/modules/GaussianBlur.h"
#include "gpu/shaders/modules/GlassUDFTentBlur.h"
#include "gpu/shaders/modules/Gradient.h"
#include "gpu/shaders/modules/HairlineLineGeometry.h"
#include "gpu/shaders/modules/HairlineQuadGeometry.h"
#include "gpu/shaders/modules/LocalMask.h"
#include "gpu/shaders/modules/MaskTexture.h"
#include "gpu/shaders/modules/MeshGeometry.h"
#include "gpu/shaders/modules/QuadColorGeometry.h"
#include "gpu/shaders/modules/QuadGeometry.h"
#include "gpu/shaders/modules/QuadTextureGeometry.h"
#include "gpu/shaders/modules/RRectGeometry.h"
#include "gpu/shaders/modules/RectGeometry.h"
#include "gpu/shaders/modules/RoundStrokeRectGeometry.h"
#include "gpu/shaders/modules/ShapeInstancedGeometry.h"
#include "gpu/shaders/modules/Texture.h"
#include "gpu/shaders/modules/TiledTexture.h"
#include "gpu/shaders/modules/Xfer.h"
#include "gpu/shaders/modules/YUVTexture.h"

namespace tgfx {

namespace {

std::array<float, 4> GetRTAdjustArray(const RenderTarget* renderTarget) {
  std::array<float, 4> result = {};
  result[0] = 2.f / static_cast<float>(renderTarget->width());
  result[2] = 2.f / static_cast<float>(renderTarget->height());
  result[1] = -1.f;
  result[3] = -1.f;
  if (renderTarget->origin() == ImageOrigin::BottomLeft) {
    result[2] = -result[2];
    result[3] = -result[3];
  }
  if (renderTarget->getContext()->backend() == Backend::WebGPU &&
      renderTarget->origin() == ImageOrigin::TopLeft) {
    result[2] = -result[2];
    result[3] = -result[3];
  }
  return result;
}

// Frame (slot 0): the render-target adjust vector.
void WriteFrame(const ProgramInfo& info, const Layout& layout, uint8_t slot, ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(slot);
  const auto adjust = GetRTAdjustArray(info.getRenderTarget());
  buffer->set(slice.fields[0], adjust);
}

// RectGeometry (the DefaultGP geometry): the view transform and the paint color uniform. The
// per-vertex coverage rides the varying and needs no write.
void WriteRectGeometry(const ProgramInfo& info, const Layout& layout, uint8_t slot,
                       ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(slot);
  const auto* geometry = static_cast<const DefaultGeometryProcessor*>(info.getGeometryProcessor());
  buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "RectGeometry", "Matrix")],
              geometry->getViewMatrix());
  buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "RectGeometry", "Color")],
              geometry->getCommonColor());
}

// QuadGeometry (the QuadPerEdgeAA geometry): the paint color uniform; the position is already in
// device space, so there is no view transform.
void WriteQuadGeometry(const ProgramInfo& info, const Layout& layout, uint8_t slot,
                       ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(slot);
  const auto* geometry =
      static_cast<const QuadPerEdgeAAGeometryProcessor*>(info.getGeometryProcessor());
  buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "QuadGeometry", "Color")],
              geometry->getCommonColor());
}

void WriteCoordMatrix(const ProgramInfo& info, const Layout& layout, const char* moduleName,
                      uint8_t slot, ParamBuffer* buffer);

/// The transform at the given position in the FP tree's coordinate-transform sequence; a negative
/// index selects the last one.
void WriteCoordMatrixAt(const ProgramInfo& info, const Layout& layout, const char* moduleName,
                        uint8_t slot, ParamBuffer* buffer, int index);

void WriteTextureLeafParams(const AOTTextureParameters& leaf, const Layout& layout,
                            const char* moduleName, uint8_t slot, ParamBuffer* buffer);

// ComplexRRectGeometry: the color pair. The corner data and the SDF run entirely on the varyings.
void WriteComplexRRectGeometry(const ProgramInfo& info, const Layout& layout, uint8_t slot,
                               ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(slot);
  const auto* geometry =
      static_cast<const ComplexNonAARRectGeometryProcessor*>(info.getGeometryProcessor());
  buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "ComplexRRectGeometry", "Color")],
              geometry->getCommonColor());
  buffer->set(
      slice.fields[layout.FieldIndexInSlice(slice, "ComplexRRectGeometry", "HasCommonColor")],
      static_cast<int32_t>(geometry->hasCommonColor() ? 1 : 0));
}

// MeshGeometry: the view transform, the color pair and the texture coordinate chain.
void WriteMeshGeometry(const ProgramInfo& info, const Layout& layout, uint8_t slot,
                       ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(slot);
  const auto* geometry = static_cast<const MeshGeometryProcessor*>(info.getGeometryProcessor());
  buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "MeshGeometry", "Matrix")],
              geometry->getViewMatrix());
  buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "MeshGeometry", "Color")],
              geometry->getCommonColor());
  buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "MeshGeometry", "HasCommonColor")],
              static_cast<int32_t>(geometry->getHasColors() ? 0 : 1));
  WriteCoordMatrix(info, layout, "MeshGeometry", slot, buffer);
}

// ShapeInstancedGeometry: the local-coordinate recovery, the view transform and the sampling
// coordinate. The instance offset and the instance color ride the vertex attributes. The sampling
// transform is the LAST one in the tree: the color graph's transforms come first, so a draw whose
// only fragment processors are the color ones leaves this unwritten (the instanced fill family
// carries no processor at all), and a coverage texture lands on it.
void WriteShapeInstancedGeometry(const ProgramInfo& info, const Layout& layout, uint8_t slot,
                                 ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(slot);
  const auto* geometry =
      static_cast<const ShapeInstancedGeometryProcessor*>(info.getGeometryProcessor());
  buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "ShapeInstancedGeometry", "UVMatrix")],
              geometry->getUVMatrix());
  buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "ShapeInstancedGeometry", "ViewMatrix")],
              geometry->getViewMatrix());
  WriteCoordMatrixAt(info, layout, "ShapeInstancedGeometry", slot, buffer, -1);
}

// CoverageTexture: the leaf's subset rect and read gates, the same values BindTextureSourceLeaf
// uploads. The sample coordinate rides the geometry's own transform.
void WriteCoverageTexture(const AOTTextureParameters& leaf, const Layout& layout, uint8_t slot,
                          ParamBuffer* buffer) {
  WriteTextureLeafParams(leaf, layout, "CoverageTexture", slot, buffer);
}

// Gradient: the clamped single-interval gradient's whole record — the endpoints, the boundary
// colors, the layout branch and the conic layout's bias/scale pair. The values are the ones
// BindGradientNode uploads for the legacy instanced-shape kernel; the coordinate transform rides
// the vertex block.
void WriteGradient(const AOTGradientParameters& params, const Layout& layout, uint8_t slot,
                   ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(slot);
  auto put = [&](const char* name, auto&& value) {
    const auto index = layout.FieldIndexInSlice(slice, "Gradient", name);
    if (index < slice.count) {
      buffer->set(slice.fields[index], value);
    }
  };
  put("CoordMatrix", params.coordMatrix);
  put("LeftBorderColor", params.leftBorder);
  put("RightBorderColor", params.rightBorder);
  put("LayoutType", static_cast<int32_t>(params.layoutType));
  // The conic layout is the only one the shared gradient layout maths reads these two for; the
  // other layouts record their defaults, which the shader never touches.
  put("Bias", params.bias);
  put("Scale", params.scale);
  put("Start", params.start);
  put("End", params.end);
}

void WriteBlurSourceGeometry(const ProgramInfo& info, const Layout& layout, uint8_t slot,
                             ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(slot);
  const auto* geometry = info.getGeometryProcessor();
  Matrix view = Matrix::I();
  if (IsProcessor<DefaultGeometryProcessor>(geometry)) {
    view = static_cast<const DefaultGeometryProcessor*>(geometry)->getViewMatrix();
  }
  buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "BlurSourceGeometry", "Matrix")], view);
  WriteCoordMatrix(info, layout, "BlurSourceGeometry", slot, buffer);
}

/// The child's sampling fields the blur-style bodies share: the tiled recipe or the plain subset
/// rect, plus the tiled-child selector.
void WriteChildSamplingFields(const AOTTextureParameters* child, const TextureView* view,
                              const char* moduleName, const Layout& layout, uint8_t slot,
                              ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(slot);
  auto put = [&](const char* name, auto&& value) {
    const auto index = layout.FieldIndexInSlice(slice, moduleName, name);
    if (index < slice.count) {
      buffer->set(slice.fields[index], value);
    } else {
      LOGE("WriteChildSamplingFields: field %s is absent from the derived layout", name);
      DEBUG_ASSERT(false);
    }
  };
  const bool tiled = child != nullptr && child->tiledRecipe.has_value();
  put("TiledChild", static_cast<int32_t>(tiled ? 1 : 0));
  if (tiled) {
    const auto& sampling = *child->tiledRecipe;
    put("ShaderModeX", static_cast<int32_t>(sampling.shaderModeX));
    put("ShaderModeY", static_cast<int32_t>(sampling.shaderModeY));
    put("Subset", std::array<float, 4>{sampling.shaderSubset.left, sampling.shaderSubset.top,
                                       sampling.shaderSubset.right, sampling.shaderSubset.bottom});
    put("Clamp", std::array<float, 4>{sampling.shaderClamp.left, sampling.shaderClamp.top,
                                      sampling.shaderClamp.right, sampling.shaderClamp.bottom});
    if (sampling.usesShaderDimensions) {
      put("Dimension",
          std::array<float, 2>{sampling.shaderDimensions.x, sampling.shaderDimensions.y});
    }
  } else if (child != nullptr && view != nullptr && view->getTexture() != nullptr) {
    float rect[4] = {};
    LeafSubsetRectForSampler(child->textureProxy, view, child->subset, child->samplerState, rect);
    put("Subset", std::array<float, 4>{rect[0], rect[1], rect[2], rect[3]});
  }
}

void WriteGlassUDFTentBlur(const AOTOpaqueKernelParameters& params,
                           const AOTTextureParameters* child, const Layout& layout, uint8_t slot,
                           ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(slot);
  auto put = [&](const char* name, auto&& value) {
    const auto index = layout.FieldIndexInSlice(slice, "GlassUDFTentBlur", name);
    if (index < slice.count) {
      buffer->set(slice.fields[index], value);
    } else {
      LOGE("WriteGlassUDFTentBlur: field %s is absent from the derived layout", name);
      DEBUG_ASSERT(false);
    }
  };
  put("Radius", std::array<float, 2>{params.tentFineRadius, params.tentCoarseRadius});
  put("Field", params.tentField);
  put("InputIsPacked", params.tentInputIsPacked);
  Point stepVectors[] = {{0, 0}, {1.0f, 0}};
  if (params.tentVertical) {
    stepVectors[1] = {0, 1.0f};
  }
  auto view = child != nullptr && child->textureProxy != nullptr
                  ? child->textureProxy->getTextureView()
                  : nullptr;
  if (child != nullptr) {
    auto matrix = child->uvMatrix;
    if (view != nullptr) {
      auto scale = view->getTextureCoord(1, 1);
      matrix.postScale(scale.x, scale.y);
      if (view->origin() == ImageOrigin::BottomLeft) {
        matrix.postScale(1, -1);
        auto translate = view->getTextureCoord(0, static_cast<float>(view->height()));
        matrix.postTranslate(translate.x, translate.y);
      }
    }
    matrix.mapPoints(stepVectors, 2);
  }
  const Point step = stepVectors[1] - stepVectors[0];
  put("Step", std::array<float, 2>{step.x, step.y});
  WriteChildSamplingFields(child, view.get(), "GlassUDFTentBlur", layout, slot, buffer);
}

void WriteGaussianBlur(const AOTOpaqueKernelParameters& params, const AOTTextureParameters* child,
                       const Layout& layout, uint8_t slot, ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(slot);
  auto put = [&](const char* name, auto&& value) {
    const auto index = layout.FieldIndexInSlice(slice, "GaussianBlur", name);
    if (index < slice.count) {
      buffer->set(slice.fields[index], value);
    } else {
      LOGE("WriteGaussianBlur: field %s is absent from the derived layout", name);
      DEBUG_ASSERT(false);
    }
  };
  put("Kernel", params.packedKernel);
  put("Radius", params.kernelRadius);
  Point stepVectors[] = {{0, 0}, {params.stepLength, 0}};
  if (params.vertical) {
    stepVectors[1] = {0, params.stepLength};
  }
  auto view = child != nullptr && child->textureProxy != nullptr
                  ? child->textureProxy->getTextureView()
                  : nullptr;
  if (child != nullptr) {
    auto matrix = child->uvMatrix;
    if (view != nullptr) {
      auto scale = view->getTextureCoord(1, 1);
      matrix.postScale(scale.x, scale.y);
      if (view->origin() == ImageOrigin::BottomLeft) {
        matrix.postScale(1, -1);
        auto translate = view->getTextureCoord(0, static_cast<float>(view->height()));
        matrix.postTranslate(translate.x, translate.y);
      }
    }
    matrix.mapPoints(stepVectors, 2);
  }
  const Point step = stepVectors[1] - stepVectors[0];
  put("Step", std::array<float, 2>{step.x, step.y});
  put("AlphaChild", static_cast<int32_t>(child != nullptr && child->isAlphaOnly ? 1 : 0));
  WriteChildSamplingFields(child, view.get(), "GaussianBlur", layout, slot, buffer);
}

// The source coordinate matrix every texture leaf shares: the FP tree's first coord transform
// with the geometry's uv matrix pre-concatenated (setTransformDataHelper's composition).
Matrix SourceCoordMatrix(const ProgramInfo& info) {
  FragmentProcessor::CoordTransformIter iter(&info);
  const CoordTransform* coordTransform = iter.next();
  Matrix uvMatrix = Matrix::I();
  const auto* geometry = info.getGeometryProcessor();
  if (IsProcessor<DefaultGeometryProcessor>(geometry)) {
    uvMatrix = static_cast<const DefaultGeometryProcessor*>(geometry)->getUVMatrix();
  } else if (IsProcessor<QuadPerEdgeAAGeometryProcessor>(geometry)) {
    uvMatrix = static_cast<const QuadPerEdgeAAGeometryProcessor*>(geometry)->getUVMatrix();
  }
  Matrix combined = {};
  combined.setConcat(coordTransform != nullptr ? coordTransform->getTotalMatrix() : Matrix::I(),
                     uvMatrix);
  return combined;
}

// The leaf's subset rect and read gates, the same values BindTextureSourceLeaf uploads.
void WriteTextureLeafParams(const AOTTextureParameters& leaf, const Layout& layout,
                            const char* moduleName, uint8_t slot, ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(slot);
  auto put = [&](const char* name, auto&& value) {
    const auto index = layout.FieldIndexInSlice(slice, moduleName, name);
    if (index < slice.count) {
      buffer->set(slice.fields[index], value);
    }
  };
  auto view = leaf.textureProxy != nullptr ? leaf.textureProxy->getTextureView() : nullptr;
  if (view == nullptr || view->getTexture() == nullptr) {
    return;
  }
  float rect[4] = {};
  LeafSubsetRectForSampler(leaf.textureProxy, view.get(), leaf.subset, leaf.samplerState, rect);
  put("Subset", std::array<float, 4>{rect[0], rect[1], rect[2], rect[3]});
  put("SubsetClampOn",
      static_cast<int32_t>(LeafSubsetClampOn(leaf.textureProxy, view.get(), leaf.subset) ? 1 : 0));
  if (leaf.hasRGBAAA) {
    auto coord = view->getTextureCoord(leaf.alphaStart.x, leaf.alphaStart.y);
    put("AlphaStart", std::array<float, 2>{coord.x, coord.y});
  }
  put("AlphaOnly", static_cast<int32_t>(leaf.isAlphaOnly ? 1 : 0));
  put("HasRgbaaa", static_cast<int32_t>(leaf.hasRGBAAA ? 1 : 0));
}

// AtlasTextGeometry: the atlas size scale, the color pair and the AlphaOnly switch. The atlas
// sample supplies the coverage itself, so the geometry needs no transform.
void WriteAtlasTextGeometry(const ProgramInfo& info, const Layout& layout, uint8_t slot,
                            ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(slot);
  const auto* geometry =
      static_cast<const AtlasTextGeometryProcessor*>(info.getGeometryProcessor());
  auto put = [&](const char* name, auto&& value) {
    const auto index = layout.FieldIndexInSlice(slice, "AtlasTextGeometry", name);
    if (index < slice.count) {
      buffer->set(slice.fields[index], value);
    }
  };
  const auto& proxy = geometry->getTextureProxy();
  auto view = proxy != nullptr ? proxy->getTextureView() : nullptr;
  if (view != nullptr && view->getTexture() != nullptr) {
    const auto scale = view->getTextureCoord(1.f, 1.f);
    put("AtlasSizeInv", std::array<float, 2>{scale.x, scale.y});
  }
  put("Color", geometry->getCommonColor());
  put("HasCommonColor", static_cast<int32_t>(geometry->hasCommonColor() ? 1 : 0));
  put("AlphaOnly", static_cast<int32_t>(geometry->isAlphaOnly() ? 1 : 0));
}

// QuadTextureGeometry: the paint color the texture source multiplies by.
void WriteQuadTextureGeometry(const ProgramInfo& info, const Layout& layout, uint8_t slot,
                              ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(slot);
  const auto* geometry =
      static_cast<const QuadPerEdgeAAGeometryProcessor*>(info.getGeometryProcessor());
  auto put = [&](const char* name, auto&& value) {
    const auto index = layout.FieldIndexInSlice(slice, "QuadTextureGeometry", name);
    if (index < slice.count) {
      buffer->set(slice.fields[index], value);
    }
  };
  put("Color", geometry->getCommonColor());
  put("HasCommonColor", static_cast<int32_t>(geometry->hasCommonColor() ? 1 : 0));
}

// Texture: the plain leaf source. Both matrices carry the source coordinate transform — the
// subset bounds go through the same transform whether the geometry supplies uv coordinates or
// the position (the old kernel's two uniforms held the same value).
void WriteTexture(const ProgramInfo& info, const AOTTextureParameters& leaf, const Layout& layout,
                  uint8_t slot, ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(slot);
  // The subset bounds go through the leaf's bare coord transform (the old kernel's
  // texSubsetMatrix), while the source coordinate takes the transform with the geometry's uv
  // matrix pre-concatenated — the two differ exactly when the geometry carries a uv matrix.
  FragmentProcessor::CoordTransformIter iter(&info);
  const CoordTransform* coordTransform = iter.next();
  const Matrix subsetMatrix =
      coordTransform != nullptr ? coordTransform->getTotalMatrix() : Matrix::I();
  buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "Texture", "CoordMatrix")],
              SourceCoordMatrix(info));
  buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "Texture", "SubsetMatrix")],
              subsetMatrix);
  WriteTextureLeafParams(leaf, layout, "Texture", slot, buffer);
}

// LocalMask: the coverage mask's own coordinate transform, from the coverage TextureEffect.
void WriteLocalMask(const ProgramInfo& info, const Layout& layout, uint8_t slot,
                    ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(slot);
  for (size_t index = info.numColorFragmentProcessors(); index < info.numFragmentProcessors();
       ++index) {
    const auto* fp = info.getFragmentProcessor(index);
    if (IsProcessor<TextureEffect>(fp)) {
      Matrix uvMatrix = Matrix::I();
      const auto* geometry = info.getGeometryProcessor();
      if (IsProcessor<QuadPerEdgeAAGeometryProcessor>(geometry)) {
        uvMatrix = static_cast<const QuadPerEdgeAAGeometryProcessor*>(geometry)->getUVMatrix();
      }
      Matrix combined = {};
      combined.setConcat(
          static_cast<const TextureEffect*>(fp)->getCoordTransform().getTotalMatrix(), uvMatrix);
      buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "LocalMask", "CoordMatrix")],
                  combined);
      return;
    }
  }
}

// DeviceMaskRuntime: the runtime gate plus the same device-space transform WriteDeviceMask
// computes; a draw without a device mask keeps the branch off.
void WriteDeviceMaskRuntime(const ProgramInfo& info, const Layout& layout, uint8_t slot,
                            ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(slot);
  auto put = [&](const char* name, auto&& value) {
    const auto index = layout.FieldIndexInSlice(slice, "DeviceMaskRuntime", name);
    if (index < slice.count) {
      buffer->set(slice.fields[index], value);
    }
  };
  const DeviceSpaceTextureEffect* mask = nullptr;
  for (size_t index = info.numColorFragmentProcessors(); index < info.numFragmentProcessors();
       ++index) {
    const auto* fp = info.getFragmentProcessor(index);
    if (IsProcessor<DeviceSpaceTextureEffect>(fp)) {
      mask = static_cast<const DeviceSpaceTextureEffect*>(fp);
    }
  }
  put("HasDeviceMask", static_cast<int32_t>(mask != nullptr ? 1 : 0));
  if (mask == nullptr) {
    return;
  }
  auto deviceCoordMatrix = mask->getUVMatrix();
  const auto& proxy = mask->getTextureProxy();
  auto view = proxy != nullptr ? proxy->getTextureView() : nullptr;
  if (view == nullptr || view->getTexture() == nullptr) {
    return;
  }
  const auto scale = view->getTextureCoord(1, 1);
  deviceCoordMatrix.postScale(scale.x, scale.y);
  put("CoordMatrix", deviceCoordMatrix);
}

// YUVTexture: the source transform, the full-bounds subset and the conversion pair off the view.
void WriteYUVTexture(const ProgramInfo& info, const AOTTextureParameters& leaf,
                     const Layout& layout, uint8_t slot, ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(slot);
  auto put = [&](const char* name, auto&& value) {
    const auto index = layout.FieldIndexInSlice(slice, "YUVTexture", name);
    if (index < slice.count) {
      buffer->set(slice.fields[index], value);
    }
  };
  put("CoordMatrix", SourceCoordMatrix(info));
  WriteTextureLeafParams(leaf, layout, "YUVTexture", slot, buffer);
  auto view = leaf.textureProxy != nullptr ? leaf.textureProxy->getTextureView() : nullptr;
  if (view == nullptr || !view->isYUV()) {
    return;
  }
  auto* yuvView = static_cast<const YUVTextureView*>(view.get());
  put("LimitedRange",
      static_cast<int32_t>(IsLimitedYUVColorRange(yuvView->yuvColorSpace()) ? 1 : 0));
  if (auto conversion = YUVColorConversionMatrix(yuvView->yuvColorSpace())) {
    put("ColorConversion", *conversion);
  }
}

// QuadColorGeometry (the QuadPerEdgeAA geometry with the per-vertex color): the exact paint color
// and its flag. The per-vertex color rides the varying.
void WriteQuadColorGeometry(const ProgramInfo& info, const Layout& layout, uint8_t slot,
                            ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(slot);
  const auto* geometry =
      static_cast<const QuadPerEdgeAAGeometryProcessor*>(info.getGeometryProcessor());
  buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "QuadColorGeometry", "Color")],
              geometry->getCommonColor());
  buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "QuadColorGeometry", "HasCommonColor")],
              static_cast<int32_t>(geometry->hasCommonColor() ? 1 : 0));
}

// DeviceMask: the mask's destination-to-sample transform, the same value BindDeviceMaskLeaf
// uploads (the effect's uv matrix scaled by the texture view's normalized coord).
void WriteDeviceMask(const ProgramInfo& info, const Layout& layout, uint8_t slot,
                     ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(slot);
  const DeviceSpaceTextureEffect* mask = nullptr;
  for (size_t index = info.numColorFragmentProcessors();
       index < info.numFragmentProcessors() && mask == nullptr; ++index) {
    const auto* fp = info.getFragmentProcessor(index);
    if (IsProcessor<DeviceSpaceTextureEffect>(fp)) {
      mask = static_cast<const DeviceSpaceTextureEffect*>(fp);
    }
  }
  if (mask == nullptr) {
    return;
  }
  auto deviceCoordMatrix = mask->getUVMatrix();
  const auto& proxy = mask->getTextureProxy();
  auto view = proxy != nullptr ? proxy->getTextureView() : nullptr;
  if (view == nullptr || view->getTexture() == nullptr) {
    return;
  }
  const auto scale = view->getTextureCoord(1, 1);
  deviceCoordMatrix.postScale(scale.x, scale.y);
  buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "DeviceMask", "CoordMatrix")],
              deviceCoordMatrix);
}

void WriteClipParams(const ProgramInfo& info, const Layout& layout, const char* moduleName,
                     uint8_t slot, ParamBuffer* buffer);

void WriteAnalyticClip(const ProgramInfo& info, const Layout& layout, uint8_t slot,
                       ParamBuffer* buffer) {
  WriteClipParams(info, layout, "AnalyticClip", slot, buffer);
}

// EllipseGeometry: the color pair and the stroke selector; the ellipse data rides the varyings.
void WriteEllipseGeometry(const ProgramInfo& info, const Layout& layout, uint8_t slot,
                          ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(slot);
  const auto* geometry = static_cast<const EllipseGeometryProcessor*>(info.getGeometryProcessor());
  auto put = [&](const char* name, auto&& value) {
    const auto index = layout.FieldIndexInSlice(slice, "EllipseGeometry", name);
    if (index < slice.count) {
      buffer->set(slice.fields[index], value);
    }
  };
  put("Color", geometry->getCommonColor());
  put("HasCommonColor", static_cast<int32_t>(geometry->hasCommonColor() ? 1 : 0));
  put("StrokeEnabled", static_cast<int32_t>(geometry->isStroke() ? 1 : 0));
}

// ComplexEllipseGeometry: the color pair; the ellipse data rides the varyings.
void WriteComplexEllipseGeometry(const ProgramInfo& info, const Layout& layout, uint8_t slot,
                                 ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(slot);
  const auto* geometry =
      static_cast<const ComplexEllipseGeometryProcessor*>(info.getGeometryProcessor());
  auto put = [&](const char* name, auto&& value) {
    const auto index = layout.FieldIndexInSlice(slice, "ComplexEllipseGeometry", name);
    if (index < slice.count) {
      buffer->set(slice.fields[index], value);
    }
  };
  put("Color", geometry->getCommonColor());
  put("HasCommonColor", static_cast<int32_t>(geometry->hasCommonColor() ? 1 : 0));
}

// RoundStrokeRectGeometry: the color pair, the AA flag and the analytic clip.
void WriteRoundStrokeRectGeometry(const ProgramInfo& info, const Layout& layout, uint8_t slot,
                                  ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(slot);
  const auto* geometry =
      static_cast<const RoundStrokeRectGeometryProcessor*>(info.getGeometryProcessor());
  auto put = [&](const char* name, auto&& value) {
    const auto index = layout.FieldIndexInSlice(slice, "RoundStrokeRectGeometry", name);
    if (index < slice.count) {
      buffer->set(slice.fields[index], value);
    }
  };
  put("Color", geometry->getCommonColor());
  put("HasCommonColor", static_cast<int32_t>(geometry->hasCommonColor() ? 1 : 0));
  put("AAEnabled", static_cast<int32_t>(geometry->getAAType() == AAType::None ? 0 : 1));
}

// HairlineLineGeometry / HairlineQuadGeometry: the view matrix, the color/coverage/AA triple and
// the analytic clip the old declaration carried in the same block.
template <typename Geometry>
void WriteHairlineGeometry(const ProgramInfo& info, const Layout& layout, uint8_t slot,
                           const char* moduleName, ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(slot);
  const auto* geometry = static_cast<const Geometry*>(info.getGeometryProcessor());
  auto put = [&](const char* name, auto&& value) {
    const auto index = layout.FieldIndexInSlice(slice, moduleName, name);
    if (index < slice.count) {
      buffer->set(slice.fields[index], value);
    }
  };
  put("Matrix", geometry->getViewMatrix());
  put("Color", geometry->getCommonColor());
  put("Coverage", geometry->getCoverage());
  put("AAEnabled", static_cast<int32_t>(geometry->getAAType() == AAType::None ? 0 : 1));
  WriteClipParams(info, layout, moduleName, slot, buffer);
}

// RRectGeometry (the non-AA rounded rect): the exact common color and its flag. The 8-bit
// quantized varying needs no write; the values come from the geometry processor.
void WriteRRectGeometry(const ProgramInfo& info, const Layout& layout, uint8_t slot,
                        ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(slot);
  const auto* geometry =
      static_cast<const NonAARRectGeometryProcessor*>(info.getGeometryProcessor());
  const auto commonColor = geometry->getCommonColor();
  buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "RRectGeometry", "CommonColor")],
              commonColor.value_or(PMColor()));
  buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "RRectGeometry", "HasCommonColor")],
              static_cast<int32_t>(commonColor.has_value() ? 1 : 0));
}

/// The texture-space coordinate transform: the FP tree's coord transform at the given position
/// (local to normalized texture space), the same matrix setTransformDataHelper uploads for the
/// legacy families. A negative index selects the last transform, which is where the coverage
/// terms sit — the color graph's transforms come first. The IR's uvMatrix keeps the raw user
/// matrix, which does not carry the texture's normalization.
void WriteCoordMatrixAt(const ProgramInfo& info, const Layout& layout, const char* moduleName,
                        uint8_t slot, ParamBuffer* buffer, int index) {
  const auto slice = layout.SliceOf(slot);
  FragmentProcessor::CoordTransformIter iter(&info);
  const CoordTransform* coordTransform = nullptr;
  int position = 0;
  while (const CoordTransform* candidate = iter.next()) {
    if (index < 0) {
      coordTransform = candidate;
    } else {
      if (position == index) {
        coordTransform = candidate;
        break;
      }
      ++position;
    }
  }
  if (coordTransform == nullptr) {
    return;
  }
  // setTransformDataHelper equivalents concatenate the geometry's uv matrix onto the transform
  // (a masked path draw carries its mask offset there); the transform's own matrix alone leaves
  // the sample offset out and the coverage lands in the wrong place.
  Matrix uvMatrix = Matrix::I();
  const auto* geometry = info.getGeometryProcessor();
  if (IsProcessor<DefaultGeometryProcessor>(geometry)) {
    uvMatrix = static_cast<const DefaultGeometryProcessor*>(geometry)->getUVMatrix();
  } else if (IsProcessor<QuadPerEdgeAAGeometryProcessor>(geometry)) {
    uvMatrix = static_cast<const QuadPerEdgeAAGeometryProcessor*>(geometry)->getUVMatrix();
  }
  Matrix combined = {};
  combined.setConcat(coordTransform->getTotalMatrix(), uvMatrix);
  buffer->set(slice.fields[layout.FieldIndexInSlice(slice, moduleName, "CoordMatrix")], combined);
}

// The first transform's matrix: every family whose color source is the sole sampler reads it
// through this form.
void WriteCoordMatrix(const ProgramInfo& info, const Layout& layout, const char* moduleName,
                      uint8_t slot, ParamBuffer* buffer) {
  WriteCoordMatrixAt(info, layout, moduleName, slot, buffer, 0);
}

// TiledTexture: the single-tap or full nine-mode recipe, field for field the same values
// BindTiledSourceLeaf uploads for the legacy families.
void WriteTiledTexture(const ProgramInfo& info, const AOTTextureParameters& leaf,
                       const Layout& layout, uint8_t slot, ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(slot);
  WriteCoordMatrix(info, layout, "TiledTexture", slot, buffer);
  if (!leaf.tiledRecipe.has_value()) {
    return;
  }
  const auto& sampling = *leaf.tiledRecipe;
  buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "TiledTexture", "ShaderModeX")],
              static_cast<int32_t>(sampling.shaderModeX));
  buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "TiledTexture", "ShaderModeY")],
              static_cast<int32_t>(sampling.shaderModeY));
  const float subset[4] = {sampling.shaderSubset.left, sampling.shaderSubset.top,
                           sampling.shaderSubset.right, sampling.shaderSubset.bottom};
  buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "TiledTexture", "Subset")], subset);
  const float clampRect[4] = {sampling.shaderClamp.left, sampling.shaderClamp.top,
                              sampling.shaderClamp.right, sampling.shaderClamp.bottom};
  buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "TiledTexture", "Clamp")], clampRect);
  if (sampling.usesShaderDimensions) {
    const float dimensions[2] = {sampling.shaderDimensions.x, sampling.shaderDimensions.y};
    buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "TiledTexture", "Dimension")],
                dimensions);
  }
  buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "TiledTexture", "AlphaOnly")],
              static_cast<int32_t>(sampling.alphaOnly ? 1 : 0));
  buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "TiledTexture", "Strict")],
              static_cast<int32_t>(sampling.strict ? 1 : 0));
  // The runtime texture read swizzle (GRAY_8/RG_88) is only read by the full nine-mode path; its
  // field is absent from the single-tap variants. Same value GLSLTiledTextureEffect uploads.
  const auto swizzleIndex = layout.FieldIndexInSlice(slice, "TiledTexture", "ReadSwizzle");
  if (swizzleIndex < slice.count) {
    auto view = leaf.textureProxy != nullptr ? leaf.textureProxy->getTextureView() : nullptr;
    if (view != nullptr && view->getTexture() != nullptr) {
      buffer->set(slice.fields[swizzleIndex],
                  static_cast<int32_t>(AOTReadSwizzleMode(view->getTexture()->format())));
    }
  }
}

// MaskTexture: the rasterized shape mask's coordinate transform. The texture itself is bound by
// the sampler path; the shader samples it and folds the red channel into the coverage.
void WriteMaskTexture(const ProgramInfo& info, const Layout& layout, uint8_t slot,
                      ParamBuffer* buffer) {
  WriteCoordMatrix(info, layout, "MaskTexture", slot, buffer);
}

// AnalyticClip: the runtime clip contract. The values mirror GLSLRectEffect's and
// GLSLRRectEffect's onSetData uploads field for field.
/// Writes the analytic clip parameters (slot's module fields) from the draw's IR analytic term.
/// The hairline families carry the same fields inside their geometry module, so the reader takes
/// the module name instead of hard-coding "AnalyticClip".
void WriteClipParams(const ProgramInfo& info, const Layout& layout, const char* moduleName,
                     uint8_t slot, ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(slot);
  auto put = [&](const char* name, auto&& value) {
    const auto index = layout.FieldIndexInSlice(slice, moduleName, name);
    if (index < slice.count) {
      buffer->set(slice.fields[index], value);
    }
  };
  // The lowering already classified the clip into the IR's analytic term and copied every
  // parameter into its node chain (RectCoverage / RRectCoverage), so the writer reads the draw's
  // IR instead of searching the processor tree — the same values, sourced from the one place that
  // owns them.
  const AOTCoverageTerm* analytic = nullptr;
  const DrawIR* ir = info.getCarriedIR();
  if (ir != nullptr) {
    for (const auto& term : ir->coverage) {
      if (term.kind == AOTCoverageTermKind::Analytic) {
        analytic = &term;
        break;
      }
    }
  }
  if (analytic != nullptr) {
    for (size_t index = 0; index < analytic->graph.nodeCount(); ++index) {
      const auto* node =
          analytic->graph.nodeAt(static_cast<AOTNodeID>(static_cast<uint32_t>(index)));
      if (node == nullptr) {
        continue;
      }
      if (const auto* rect = std::get_if<AOTRectCoverageParameters>(&node->parameters)) {
        const Matrix deviceToLocal =
            Matrix::MakeAll(rect->deviceToLocal[0], rect->deviceToLocal[1], rect->deviceToLocal[2],
                            rect->deviceToLocal[3], rect->deviceToLocal[4], rect->deviceToLocal[5],
                            rect->deviceToLocal[6], rect->deviceToLocal[7], rect->deviceToLocal[8]);
        put("LocalRect",
            std::array<float, 4>{rect->rect[0], rect->rect[1], rect->rect[2], rect->rect[3]});
        // The RectCoverage node is the AA form: the lowering routes hard-edged rects through the
        // rrect node with a zero antiAlias.
        put("AntiAlias", 1.0f);
        put("DeviceToLocal", deviceToLocal);
        if (deviceToLocal == Matrix::I()) {
          // The device-space form uploads half a pixel outward so the coverage ramp centers on the
          // geometric boundary (GLSLRectEffect::onSetData).
          put("Rect", std::array<float, 4>{rect->rect[0] - 0.5f, rect->rect[1] - 0.5f,
                                           rect->rect[2] + 0.5f, rect->rect[3] + 0.5f});
          put("HasClip", static_cast<int32_t>(1));
        } else {
          put("HasClip", static_cast<int32_t>(3));
        }
        return;
      }
      if (const auto* rrect = std::get_if<AOTRRectCoverageParameters>(&node->parameters)) {
        put("LocalRect",
            std::array<float, 4>{rrect->rect[0], rrect->rect[1], rrect->rect[2], rrect->rect[3]});
        put("RadiiX", rrect->radiiX);
        put("RadiiY", rrect->radiiY);
        put("AntiAlias", rrect->antiAlias);
        put("DeviceToLocal", Matrix::MakeAll(rrect->deviceToLocal[0], rrect->deviceToLocal[1],
                                             rrect->deviceToLocal[2], rrect->deviceToLocal[3],
                                             rrect->deviceToLocal[4], rrect->deviceToLocal[5],
                                             rrect->deviceToLocal[6], rrect->deviceToLocal[7],
                                             rrect->deviceToLocal[8]));
        put("HasClip", static_cast<int32_t>(2));
        return;
      }
    }
  }
  // No clip: the contract branch reads nothing but the flag.
  put("HasClip", static_cast<int32_t>(0));
}

// Xfer: the blend mode, the dst-texture coord mapping for the dst dialect, and the render-target
// write swizzle — the same values BindPorterDuffXfer uploads for the legacy families.
void WriteXfer(const ProgramInfo& info, const Layout& layout, uint8_t slot, ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(slot);
  if (slice.count == 0) {
    return;
  }
  // Every write goes through an index check: the dst-dialect fields are absent when the draw's
  // HAS_XP dim is 0, and a wrong name must degrade to a skipped field, never an out-of-range one.
  auto put = [&](const char* name, auto value) {
    const auto index = layout.FieldIndexInSlice(slice, "Xfer", name);
    if (index < slice.count) {
      buffer->set(slice.fields[index], value);
    }
  };
  // The write swizzle is unconditional (the HAS_XP == 0 path composes too).
  put("OutputAlphaSwizzle",
      static_cast<int32_t>(info.getOutputSwizzle() == Swizzle::AAAA() ? 1 : 0));
  const auto* processor = info.getXferProcessor();
  // Only the PorterDuff processor carries a blend mode and a dst texture; the passthrough XP
  // (the HAS_XP == 0 variants) declares neither field.
  if (!IsProcessor<PorterDuffXferProcessor>(processor)) {
    return;
  }
  const auto* porterDuff = static_cast<const PorterDuffXferProcessor*>(processor);
  put("XPBlendMode", static_cast<int32_t>(porterDuff->getBlendMode()));
  const auto& dstInfo = porterDuff->getDstTextureInfo();
  if (dstInfo.textureProxy == nullptr) {
    return;
  }
  auto dstView = dstInfo.textureProxy->getTextureView();
  if (dstView == nullptr || dstView->getTexture() == nullptr) {
    return;
  }
  const std::array<float, 2> upperLeft = {dstInfo.offset.x, dstInfo.offset.y};
  put("DstTextureUpperLeft", upperLeft);
  int width = dstView->width();
  int height = dstView->height();
  if (dstView->getTexture()->type() == TextureType::Rectangle) {
    width = 1;
    height = 1;
  }
  const std::array<float, 2> scales = {1.f / static_cast<float>(width),
                                       1.f / static_cast<float>(height)};
  put("DstTextureCoordScale", scales);
}

}  // namespace

/// The declaration-driven families' writer: every field write goes through the derived layout
/// (ParamBuffer addressing), dispatched by the module in each slot, with no name lookup and no
/// processor-tree walk for the values.
class DeclaredFamilyProgramWriter final : public PrecompiledProgramWriter {
 public:
  bool canWrite(const ProgramInfo& info) const override;

 protected:
  void writeEngineDefaults(const ProgramInfo& info,
                           UniformData* fragmentUniformData) const override;
  void writeGeometryProcessor(const ProgramInfo& info, UniformData* vertexUniformData,
                              UniformData* fragmentUniformData) const override;
  void writeProcessors(const ProgramInfo& info, UniformData* vertexUniformData,
                       UniformData* fragmentUniformData) const override;
};

bool DeclaredFamilyProgramWriter::canWrite(const ProgramInfo&) const {
  return true;
}

void DeclaredFamilyProgramWriter::writeEngineDefaults(const ProgramInfo&, UniformData*) const {
}

void DeclaredFamilyProgramWriter::writeGeometryProcessor(const ProgramInfo& info,
                                                         UniformData* vertexUniformData,
                                                         UniformData* fragmentUniformData) const {
  const auto shape = ExtractDrawShape(&info);
  const auto match = MatchAssembledFamily(shape);
  if (!match) {
    LOGE(
        "DeclaredFamilyProgramWriter: the draw no longer matches a declared family; uniforms "
        "stay unwritten");
    return;
  }
  const Family& family = Families()[match->variant.family];
  const auto layout = DeriveLayout(match->variant);
#if DEBUG
  // The pairing contract, spelled the way the program creator applies it: a shader that declares
  // one input per geometry attribute pairs in order (the count must match), and one that reads a
  // strict subset pairs by physical slot (every declared location must be a slot the geometry
  // actually provides). A violation here means the module's element order disagrees with the
  // geometry's declared attribute order, which would otherwise surface only as pixel damage.
  {
    const auto& geometryAttributes = info.getVertexAttributes();
    const auto& geometrySlots = info.getGeometryProcessor()->vertexAttributeLocations();
    // Instance inputs occupy the slots after the vertex ones (the creator's own split), so the
    // pairing checks below see the vertex inputs alone.
    uint32_t instanceBase = 0;
    for (auto slot : geometrySlots) {
      instanceBase = std::max(instanceBase, slot + 1);
    }
    size_t vertexInputCount = 0;
    for (const auto& attribute : layout.attributes) {
      if (attribute.location < instanceBase) {
        ++vertexInputCount;
      }
    }
    if (vertexInputCount > geometryAttributes.size()) {
      LOGE(
          "DeclaredFamilyProgramWriter: %s declares %zu vertex inputs but the geometry provides "
          "%zu attributes — the module's element list is out of sync with the geometry",
          family.name, vertexInputCount, geometryAttributes.size());
      DEBUG_ASSERT(false);
    }
    // Only a strict subset pairs by physical slot (the creator parks the unread attributes); a
    // one-to-one interface pairs in order, where the declared locations are internal numbering.
    if (vertexInputCount < geometryAttributes.size()) {
      for (const auto& attribute : layout.attributes) {
        if (attribute.location >= instanceBase) {
          continue;
        }
        const bool onSlot = std::any_of(
            geometrySlots.begin(), geometrySlots.end(),
            [location = attribute.location](uint32_t slot) { return slot == location; });
        if (!onSlot) {
          LOGE(
              "DeclaredFamilyProgramWriter: attribute %s wants location %u, which the geometry "
              "does not declare — the module's element order disagrees with the geometry's "
              "attribute order",
              attribute.name.c_str(), attribute.location);
          DEBUG_ASSERT(false);
        }
      }
    }
  }
#endif
  const auto modules = ModulesOf(family, match->variant);
  ParamBuffer buffer(vertexUniformData, fragmentUniformData);
  for (uint8_t slot = 0; slot < modules.count; ++slot) {
    const Module* module = modules.list[slot];
    if (module == nullptr || !ModulePresent(family, match->variant, slot)) {
      continue;
    }
    if (module == &FrameModule()) {
      WriteFrame(info, layout, slot, &buffer);
    } else if (module == &RectGeometry) {
      WriteRectGeometry(info, layout, slot, &buffer);
    } else if (module == &BlurSourceGeometry) {
      WriteBlurSourceGeometry(info, layout, slot, &buffer);
    } else if (module == &RoundStrokeRectGeometry) {
      WriteRoundStrokeRectGeometry(info, layout, slot, &buffer);
    } else if (module == &ComplexEllipseGeometry) {
      WriteComplexEllipseGeometry(info, layout, slot, &buffer);
    } else if (module == &EllipseGeometry) {
      WriteEllipseGeometry(info, layout, slot, &buffer);
    } else if (module == &HairlineLineGeometry) {
      WriteHairlineGeometry<HairlineLineGeometryProcessor>(info, layout, slot,
                                                           "HairlineLineGeometry", &buffer);
    } else if (module == &HairlineQuadGeometry) {
      WriteHairlineGeometry<HairlineQuadGeometryProcessor>(info, layout, slot,
                                                           "HairlineQuadGeometry", &buffer);
    } else if (module == &ComplexRRectGeometry) {
      WriteComplexRRectGeometry(info, layout, slot, &buffer);
    } else if (module == &MeshGeometry) {
      WriteMeshGeometry(info, layout, slot, &buffer);
    } else if (module == &ShapeInstancedGeometry) {
      WriteShapeInstancedGeometry(info, layout, slot, &buffer);
    } else if (module == &QuadColorGeometry) {
      WriteQuadColorGeometry(info, layout, slot, &buffer);
    } else if (module == &QuadGeometry) {
      WriteQuadGeometry(info, layout, slot, &buffer);
    } else if (module == &RRectGeometry) {
      WriteRRectGeometry(info, layout, slot, &buffer);
    } else if (module == &AtlasTextGeometry) {
      WriteAtlasTextGeometry(info, layout, slot, &buffer);
    } else if (module == &QuadTextureGeometry) {
      WriteQuadTextureGeometry(info, layout, slot, &buffer);
    } else if (module == &Texture) {
      if (match->tiledLeaf != nullptr) {
        WriteTexture(info, *match->tiledLeaf, layout, slot, &buffer);
      }
    } else if (module == &CoverageTexture) {
      if (match->maskLeaf != nullptr) {
        WriteCoverageTexture(*match->maskLeaf, layout, slot, &buffer);
      }
    } else if (module == &GaussianBlur) {
      if (match->blurKernel != nullptr) {
        WriteGaussianBlur(*match->blurKernel, match->blurChild, layout, slot, &buffer);
      }
    } else if (module == &GlassUDFTentBlur) {
      if (match->blurKernel != nullptr) {
        WriteGlassUDFTentBlur(*match->blurKernel, match->blurChild, layout, slot, &buffer);
      }
    } else if (module == &Gradient) {
      if (match->gradient != nullptr) {
        WriteGradient(*match->gradient, layout, slot, &buffer);
      }
    } else if (module == &LocalMask) {
      WriteLocalMask(info, layout, slot, &buffer);
    } else if (module == &DeviceMaskRuntime) {
      WriteDeviceMaskRuntime(info, layout, slot, &buffer);
    } else if (module == &YUVTexture) {
      if (match->tiledLeaf != nullptr) {
        WriteYUVTexture(info, *match->tiledLeaf, layout, slot, &buffer);
      }
    } else if (module == &TiledTexture) {
      if (match->tiledLeaf != nullptr) {
        WriteTiledTexture(info, *match->tiledLeaf, layout, slot, &buffer);
      }
    } else if (module == &DeviceMask) {
      WriteDeviceMask(info, layout, slot, &buffer);
    } else if (module == &MaskTexture) {
      WriteMaskTexture(info, layout, slot, &buffer);
    } else if (module == &AnalyticClip) {
      WriteAnalyticClip(info, layout, slot, &buffer);
    } else if (module == &XferModule()) {
      WriteXfer(info, layout, slot, &buffer);
    }
  }
}

void DeclaredFamilyProgramWriter::writeProcessors(const ProgramInfo&, UniformData*,
                                                  UniformData*) const {
  // Every value write happens in writeGeometryProcessor's module walk.
}

std::unique_ptr<ProgramWriter> MakeDeclaredFamilyProgramWriter() {
  return std::make_unique<DeclaredFamilyProgramWriter>();
}

}  // namespace tgfx
