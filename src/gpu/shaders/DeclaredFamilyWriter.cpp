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
#include "gpu/aot/ir/EffectGraph.h"
#include "gpu/aot/select/FamilyMatch.h"
#include "gpu/processors/DefaultGeometryProcessor.h"
#include "gpu/processors/NonAARRectGeometryProcessor.h"
#include "gpu/processors/PorterDuffXferProcessor.h"
#include "gpu/processors/QuadPerEdgeAAGeometryProcessor.h"
#include "gpu/processors/RRectEffect.h"
#include "gpu/processors/RectEffect.h"
#include "gpu/shaders/Families.h"
#include "gpu/shaders/ParamBuffer.h"
#include "gpu/shaders/contract/DrawFacts.h"
#include "gpu/shaders/modules/AnalyticClip.h"
#include "gpu/shaders/modules/Frame.h"
#include "gpu/shaders/modules/MaskTexture.h"
#include "gpu/shaders/modules/QuadGeometry.h"
#include "gpu/shaders/modules/RRectGeometry.h"
#include "gpu/shaders/modules/RectGeometry.h"
#include "gpu/shaders/modules/TiledTexture.h"
#include "gpu/shaders/modules/Xfer.h"

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

/// The texture-space coordinate transform: the FP tree's first coord transform (local to
/// normalized texture space), the same matrix setTransformDataHelper uploads for the legacy
/// families. The IR's uvMatrix keeps the raw user matrix, which does not carry the texture's
/// normalization.
void WriteCoordMatrix(const ProgramInfo& info, const Layout& layout, const char* moduleName,
                      uint8_t slot, ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(slot);
  FragmentProcessor::CoordTransformIter iter(&info);
  const CoordTransform* coordTransform = iter.next();
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
void WriteAnalyticClip(const ProgramInfo& info, const Layout& layout, uint8_t slot,
                       ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(slot);
  const RectEffect* rectClip = nullptr;
  const RRectEffect* rrectClip = nullptr;
  for (size_t index = 0; index < info.numFragmentProcessors(); ++index) {
    FragmentProcessor::Iter iter(info.getFragmentProcessor(index));
    const FragmentProcessor* fp = iter.next();
    while (fp) {
      if (rectClip == nullptr && IsProcessor<RectEffect>(fp)) {
        rectClip = static_cast<const RectEffect*>(fp);
      } else if (rrectClip == nullptr && IsProcessor<RRectEffect>(fp)) {
        rrectClip = static_cast<const RRectEffect*>(fp);
      }
      fp = iter.next();
    }
  }
  if (rectClip != nullptr) {
    buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "AnalyticClip", "LocalRect")],
                rectClip->getRect());
    buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "AnalyticClip", "AntiAlias")],
                rectClip->isAntiAlias() ? 1.0f : 0.0f);
    buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "AnalyticClip", "DeviceToLocal")],
                rectClip->getDeviceToLocal());
    if (rectClip->isDeviceSpaceRect()) {
      // The device-space rect uploads half a pixel outward so the coverage ramp centers on the
      // geometric boundary (GLSLRectEffect::onSetData).
      buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "AnalyticClip", "Rect")],
                  rectClip->getRect().makeOutset(0.5f, 0.5f));
      buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "AnalyticClip", "HasClip")],
                  static_cast<int32_t>(1));
    } else {
      buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "AnalyticClip", "HasClip")],
                  static_cast<int32_t>(3));
    }
    return;
  }
  if (rrectClip != nullptr) {
    buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "AnalyticClip", "LocalRect")],
                rrectClip->getLocalRect());
    const auto& radii = rrectClip->getRadii();
    const float radiiX[4] = {radii[0].x, radii[1].x, radii[2].x, radii[3].x};
    const float radiiY[4] = {radii[0].y, radii[1].y, radii[2].y, radii[3].y};
    buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "AnalyticClip", "RadiiX")], radiiX);
    buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "AnalyticClip", "RadiiY")], radiiY);
    buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "AnalyticClip", "AntiAlias")],
                rrectClip->isAntiAlias() ? 1.0f : 0.0f);
    buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "AnalyticClip", "DeviceToLocal")],
                rrectClip->getDeviceToLocal());
    buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "AnalyticClip", "HasClip")],
                static_cast<int32_t>(2));
    return;
  }
  // No clip: the contract branch reads nothing but the flag.
  buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "AnalyticClip", "HasClip")],
              static_cast<int32_t>(0));
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
    } else if (module == &QuadGeometry) {
      WriteQuadGeometry(info, layout, slot, &buffer);
    } else if (module == &RRectGeometry) {
      WriteRRectGeometry(info, layout, slot, &buffer);
    } else if (module == &TiledTexture) {
      if (match->tiledLeaf != nullptr) {
        WriteTiledTexture(info, *match->tiledLeaf, layout, slot, &buffer);
      }
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
