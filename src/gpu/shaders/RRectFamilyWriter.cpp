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
//  either express or implied. See the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/ProgramInfo.h"
#include "gpu/ProgramWriter.h"
#include "gpu/UniformData.h"
#include "gpu/aot/ir/EffectGraph.h"
#include "gpu/aot/select/FamilyMatch.h"
#include "gpu/processors/NonAARRectGeometryProcessor.h"
#include "gpu/processors/PorterDuffXferProcessor.h"
#include "gpu/processors/RRectEffect.h"
#include "gpu/processors/RectEffect.h"
#include "gpu/shaders/Families.h"
#include "gpu/shaders/ParamBuffer.h"
#include "gpu/shaders/contract/DrawFacts.h"

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
void WriteFrame(const ProgramInfo& info, const Layout& layout, ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(0);
  const auto adjust = GetRTAdjustArray(info.getRenderTarget());
  buffer->set(slice.fields[0], adjust);
}

// RRectGeometry (slot 1): the exact common color and its flag. The 8-bit quantized varying needs
// no write; the values come from the geometry processor, the instance-time truth.
void WriteRRectGeometry(const ProgramInfo& info, const Layout& layout, ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(1);
  const auto* geometry =
      static_cast<const NonAARRectGeometryProcessor*>(info.getGeometryProcessor());
  const auto commonColor = geometry->getCommonColor();
  buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "RRectGeometry", "CommonColor")],
              commonColor.value_or(PMColor()));
  buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "RRectGeometry", "HasCommonColor")],
              static_cast<int32_t>(commonColor.has_value() ? 1 : 0));
}

// TiledTexture (slot 2): the single-tap tiled recipe, field-for-field the same values
// BindTiledSourceLeaf uploads for the legacy families.
void WriteTiledTexture(const ProgramInfo& info, const AOTTextureParameters& leaf,
                       const Layout& layout, ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(2);
  // The coordinate chain is the FP tree's coord transform (local to normalized texture space),
  // the same matrix setTransformDataHelper uploads for the legacy families: the IR's uvMatrix
  // keeps the raw user matrix, which does not carry the texture's normalization.
  FragmentProcessor::CoordTransformIter iter(&info);
  const CoordTransform* coordTransform = iter.next();
  if (coordTransform != nullptr) {
    buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "TiledTexture", "CoordMatrix")],
                coordTransform->getTotalMatrix());
  }
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
}

// AnalyticClip (slot 3): the runtime clip contract. The values mirror GLSLRectEffect's and
// GLSLRRectEffect's onSetData uploads field for field.
void WriteAnalyticClip(const ProgramInfo& info, const Layout& layout, ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(3);
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

// Xfer (slot 4): the blend mode and, for the dst-texture dialect, the coord mapping — the same
// values BindPorterDuffXfer uploads for the legacy families.
void WriteXfer(const ProgramInfo& info, const Layout& layout, ParamBuffer* buffer) {
  const auto slice = layout.SliceOf(4);
  if (slice.count == 0) {
    return;
  }
  const auto* processor = info.getXferProcessor();
  if (processor == nullptr) {
    return;
  }
  const auto* porterDuff = static_cast<const PorterDuffXferProcessor*>(processor);
  buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "Xfer", "XPBlendMode")],
              static_cast<int32_t>(porterDuff->getBlendMode()));
  const auto& dstInfo = porterDuff->getDstTextureInfo();
  if (dstInfo.textureProxy == nullptr) {
    return;
  }
  auto dstView = dstInfo.textureProxy->getTextureView();
  if (dstView == nullptr) {
    return;
  }
  const float upperLeft[2] = {dstInfo.offset.x, dstInfo.offset.y};
  buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "Xfer", "DstTextureUpperLeft")],
              upperLeft);
  int width = dstView->width();
  int height = dstView->height();
  if (dstView->getTexture()->type() == TextureType::Rectangle) {
    width = 1;
    height = 1;
  }
  const float scales[2] = {1.f / static_cast<float>(width), 1.f / static_cast<float>(height)};
  buffer->set(slice.fields[layout.FieldIndexInSlice(slice, "Xfer", "DstTextureCoordScale")],
              scales);
}

}  // namespace

/// The declaration-driven families' writer: every field write goes through the derived layout
/// (ParamBuffer addressing), no name lookup, no processor-tree walk for the values.
class RRectProgramWriter final : public PrecompiledProgramWriter {
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

bool RRectProgramWriter::canWrite(const ProgramInfo&) const {
  return true;
}

void RRectProgramWriter::writeEngineDefaults(const ProgramInfo&, UniformData*) const {
}

void RRectProgramWriter::writeGeometryProcessor(const ProgramInfo& info,
                                                UniformData* vertexUniformData,
                                                UniformData* fragmentUniformData) const {
  const auto shape = ExtractDrawShape(&info);
  const auto match = MatchAssembledFamily(shape);
  if (!match) {
    LOGE(
        "RRectProgramWriter: the draw no longer matches the RRect family; uniforms stay "
        "unwritten");
    return;
  }
  const auto layout = DeriveLayout(match->variant);
  ParamBuffer buffer(vertexUniformData, fragmentUniformData);
  // Slot layout: Frame(0), RRectGeometry(1), TiledTexture(2), AnalyticClip(3), Xfer(4).
  WriteFrame(info, layout, &buffer);
  WriteRRectGeometry(info, layout, &buffer);
  const Family& family = Families()[match->variant.family];
  if (ModulePresent(family, match->variant, 2) && match->tiledLeaf != nullptr) {
    WriteTiledTexture(info, *match->tiledLeaf, layout, &buffer);
  }
}

void RRectProgramWriter::writeProcessors(const ProgramInfo& info, UniformData* vertexUniformData,
                                         UniformData* fragmentUniformData) const {
  const auto shape = ExtractDrawShape(&info);
  const auto match = MatchAssembledFamily(shape);
  if (!match) {
    return;
  }
  const auto layout = DeriveLayout(match->variant);
  ParamBuffer buffer(vertexUniformData, fragmentUniformData);
  WriteAnalyticClip(info, layout, &buffer);
  WriteXfer(info, layout, &buffer);
}

std::unique_ptr<ProgramWriter> MakeRRectProgramWriter() {
  return std::make_unique<RRectProgramWriter>();
}

}  // namespace tgfx
