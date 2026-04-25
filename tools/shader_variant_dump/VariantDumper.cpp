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

#include "VariantDumper.h"

#include <ctime>

#include "JsonWriter.h"
#include "gpu/ShaderModuleRegistry.h"
#include "gpu/processors/AARectEffect.h"
#include "gpu/processors/AlphaThresholdFragmentProcessor.h"
#include "gpu/processors/AtlasTextGeometryProcessor.h"
#include "gpu/processors/ClampedGradientEffect.h"
#include "gpu/processors/ColorMatrixFragmentProcessor.h"
#include "gpu/processors/ColorSpaceXFormEffect.h"
#include "gpu/processors/ComposeFragmentProcessor.h"
#include "gpu/processors/ConicGradientLayout.h"
#include "gpu/processors/ConstColorProcessor.h"
#include "gpu/processors/DefaultGeometryProcessor.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/DiamondGradientLayout.h"
#include "gpu/processors/DualIntervalGradientColorizer.h"
#include "gpu/processors/EllipseGeometryProcessor.h"
#include "gpu/processors/EmptyXferProcessor.h"
#include "gpu/processors/GaussianBlur1DFragmentProcessor.h"
#include "gpu/processors/HairlineLineGeometryProcessor.h"
#include "gpu/processors/HairlineQuadGeometryProcessor.h"
#include "gpu/processors/LinearGradientLayout.h"
#include "gpu/processors/LumaFragmentProcessor.h"
#include "gpu/processors/MeshGeometryProcessor.h"
#include "gpu/processors/NonAARRectGeometryProcessor.h"
#include "gpu/processors/PorterDuffXferProcessor.h"
#include "gpu/processors/QuadPerEdgeAAGeometryProcessor.h"
#include "gpu/processors/RadialGradientLayout.h"
#include "gpu/processors/RoundStrokeRectGeometryProcessor.h"
#include "gpu/processors/ShapeInstancedGeometryProcessor.h"
#include "gpu/processors/SingleIntervalGradientColorizer.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TextureGradientColorizer.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/processors/UnrolledBinaryGradientColorizer.h"
#include "gpu/processors/XfermodeFragmentProcessor.h"

namespace tgfx {

// Processor registry built once and returned by reference. Order here determines emission order
// in the JSON output; we group by category (GP → FP → XP) for readability.
const std::vector<VariantProcessorEntry>& AllVariantProcessors() {
  static const std::vector<VariantProcessorEntry> entries = {
      // ---- GeometryProcessor (10) ----
      {"DefaultGeometryProcessor", "GeometryProcessor",
       &DefaultGeometryProcessor::EnumerateVariants, ""},
      {"EllipseGeometryProcessor", "GeometryProcessor",
       &EllipseGeometryProcessor::EnumerateVariants, ""},
      {"HairlineLineGeometryProcessor", "GeometryProcessor",
       &HairlineLineGeometryProcessor::EnumerateVariants, ""},
      {"HairlineQuadGeometryProcessor", "GeometryProcessor",
       &HairlineQuadGeometryProcessor::EnumerateVariants, ""},
      {"MeshGeometryProcessor", "GeometryProcessor", &MeshGeometryProcessor::EnumerateVariants, ""},
      {"NonAARRectGeometryProcessor", "GeometryProcessor",
       &NonAARRectGeometryProcessor::EnumerateVariants, ""},
      {"QuadPerEdgeAAGeometryProcessor", "GeometryProcessor",
       &QuadPerEdgeAAGeometryProcessor::EnumerateVariants, ""},
      {"RoundStrokeRectGeometryProcessor", "GeometryProcessor",
       &RoundStrokeRectGeometryProcessor::EnumerateVariants, ""},
      {"ShapeInstancedGeometryProcessor", "GeometryProcessor",
       &ShapeInstancedGeometryProcessor::EnumerateVariants, ""},
      {"AtlasTextGeometryProcessor", "GeometryProcessor",
       &AtlasTextGeometryProcessor::EnumerateVariants, ""},

      // ---- FragmentProcessor (21) ----
      {"LinearGradientLayout", "FragmentProcessor", &LinearGradientLayout::EnumerateVariants, ""},
      {"RadialGradientLayout", "FragmentProcessor", &RadialGradientLayout::EnumerateVariants, ""},
      {"ConicGradientLayout", "FragmentProcessor", &ConicGradientLayout::EnumerateVariants, ""},
      {"DiamondGradientLayout", "FragmentProcessor", &DiamondGradientLayout::EnumerateVariants, ""},
      {"DeviceSpaceTextureEffect", "FragmentProcessor",
       &DeviceSpaceTextureEffect::EnumerateVariants, ""},
      {"UnrolledBinaryGradientColorizer", "FragmentProcessor",
       &UnrolledBinaryGradientColorizer::EnumerateVariants, ""},
      {"GaussianBlur1DFragmentProcessor", "FragmentProcessor",
       &GaussianBlur1DFragmentProcessor::EnumerateVariants, ""},
      {"ColorSpaceXformEffect", "FragmentProcessor", &ColorSpaceXformEffect::EnumerateVariants, ""},
      {"TextureEffect", "FragmentProcessor", &TextureEffect::EnumerateVariants, ""},
      {"TiledTextureEffect", "FragmentProcessor", &TiledTextureEffect::EnumerateVariants, ""},
      {"ConstColorProcessor", "FragmentProcessor", &ConstColorProcessor::EnumerateVariants, ""},
      // XfermodeFragmentProcessor's runtime name() is suffixed with "- two/- dst/- src", but all
      // three children share the same GLSL module. Use the "- two" form for ShaderModuleRegistry
      // lookup while exposing the bare class name as the processor identifier.
      {"XfermodeFragmentProcessor", "FragmentProcessor",
       &XfermodeFragmentProcessor::EnumerateVariants, "XfermodeFragmentProcessor - two"},
      // ComposeFragmentProcessor is a pure structural container with no GLSL module; the JSON
      // output reflects this with an empty moduleSource string.
      {"ComposeFragmentProcessor", "FragmentProcessor",
       &ComposeFragmentProcessor::EnumerateVariants, ""},
      {"ClampedGradientEffect", "FragmentProcessor", &ClampedGradientEffect::EnumerateVariants, ""},
      {"AARectEffect", "FragmentProcessor", &AARectEffect::EnumerateVariants, ""},
      // AlphaThresholdFragmentProcessor's runtime name() returns "AlphaStepFragmentProcessor".
      // Use that form for module lookup but the class name for the processor identity so the
      // JSON surface stays aligned with the codebase class names.
      {"AlphaThresholdFragmentProcessor", "FragmentProcessor",
       &AlphaThresholdFragmentProcessor::EnumerateVariants, "AlphaStepFragmentProcessor"},
      {"ColorMatrixFragmentProcessor", "FragmentProcessor",
       &ColorMatrixFragmentProcessor::EnumerateVariants, ""},
      {"LumaFragmentProcessor", "FragmentProcessor", &LumaFragmentProcessor::EnumerateVariants, ""},
      {"DualIntervalGradientColorizer", "FragmentProcessor",
       &DualIntervalGradientColorizer::EnumerateVariants, ""},
      {"SingleIntervalGradientColorizer", "FragmentProcessor",
       &SingleIntervalGradientColorizer::EnumerateVariants, ""},
      {"TextureGradientColorizer", "FragmentProcessor",
       &TextureGradientColorizer::EnumerateVariants, ""},

      // ---- XferProcessor (2) ----
      {"EmptyXferProcessor", "XferProcessor", &EmptyXferProcessor::EnumerateVariants, ""},
      {"PorterDuffXferProcessor", "XferProcessor", &PorterDuffXferProcessor::EnumerateVariants,
       ""},
  };
  return entries;
}

static std::string CurrentIsoTimestamp() {
  std::time_t now = std::time(nullptr);
  std::tm utc = {};
#ifdef _WIN32
  gmtime_s(&utc, &now);
#else
  gmtime_r(&now, &utc);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utc);
  return buf;
}

// Resolves a processor's GLSL module source. Returns an empty string for processors without a
// module (e.g. ComposeFragmentProcessor, which is a structural container).
static std::string LookupModuleSource(const VariantProcessorEntry& entry,
                                      std::string* outModuleId) {
  const std::string& lookupKey = entry.moduleLookupKey.empty() ? entry.name : entry.moduleLookupKey;
  if (!ShaderModuleRegistry::HasModule(lookupKey)) {
    if (outModuleId != nullptr) {
      *outModuleId = "";
    }
    return "";
  }
  auto id = ShaderModuleRegistry::GetModuleID(lookupKey);
  if (outModuleId != nullptr) {
    // Export the integer enum for stable identification even if the C++ enumerator names change.
    *outModuleId = std::to_string(static_cast<int>(id));
  }
  return ShaderModuleRegistry::GetModule(id);
}

void DumpAllVariants(std::ostream& out) {
  const auto& entries = AllVariantProcessors();

  // Count totals up front so the header can declare them (also a useful sanity check).
  size_t totalVariants = 0;
  for (const auto& entry : entries) {
    totalVariants += entry.enumerate().size();
  }

  JsonWriter w(out);
  w.beginObject();
  w.key("schemaVersion");
  w.valueInt(1);
  w.key("generatedAt");
  w.valueString(CurrentIsoTimestamp());
  w.key("processorCount");
  w.valueInt(static_cast<long long>(entries.size()));
  w.key("totalVariants");
  w.valueInt(static_cast<long long>(totalVariants));
  w.key("processors");
  w.beginArray();
  for (const auto& entry : entries) {
    std::string moduleId;
    std::string moduleSource = LookupModuleSource(entry, &moduleId);
    auto variants = entry.enumerate();

    w.beginObject();
    w.key("name");
    w.valueString(entry.name);
    w.key("category");
    w.valueString(entry.category);
    w.key("moduleId");
    w.valueString(moduleId);
    w.key("moduleSource");
    w.valueString(moduleSource);
    w.key("variantCount");
    w.valueInt(static_cast<long long>(variants.size()));
    w.key("variants");
    w.beginArray();
    for (const auto& variant : variants) {
      w.beginObject();
      w.key("index");
      w.valueInt(variant.index);
      w.key("name");
      w.valueString(variant.name);
      w.key("preamble");
      w.valueString(variant.preamble);
      w.key("runtimeKeyHash");
      w.valueHexU64(variant.runtimeKeyHash);
      w.endObject();
    }
    w.endArray();
    w.endObject();
  }
  w.endArray();
  w.endObject();
  out << "\n";
}

}  // namespace tgfx