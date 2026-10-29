// The aggregation every build-time uniform reconciliation walks: one table mapping each declared
// field name to the UniformFormat the reflected uniform block must carry. The entries come from
// the feature modules' Fields lists and the parameter headers; a name the table does not know is
// a field nobody declared, and a name with the wrong format is a declaration that drifted from
// the shader. The runtime keeps its own copy implicit (the writers pin their value types), so
// this header is the tool-facing view only.
#pragma once

#include <cstddef>
#include <map>
#include <set>
#include <utility>
#include "gpu/Uniform.h"
#include "gpu/shaders/modules/AnalyticClipModule.h"
#include "gpu/shaders/modules/BlendParams.h"
#include "gpu/shaders/modules/BlurKernelModule.h"
#include "gpu/shaders/modules/ChainParams.h"
#include "gpu/shaders/modules/ColorSpaceParams.h"
#include "gpu/shaders/modules/DeviceMaskModule.h"
#include "gpu/shaders/modules/EngineParams.h"
#include "gpu/shaders/modules/GradientModule.h"
#include "gpu/shaders/modules/ParamCatalog.h"
#include "gpu/shaders/modules/ParamHandle.h"
#include "gpu/shaders/modules/PerlinNoiseModule.h"
#include "gpu/shaders/modules/PointwiseOpModule.h"
#include "gpu/shaders/modules/PorterDuffXPModule.h"
#include "gpu/shaders/modules/TextureSourceModule.h"
#include "gpu/shaders/modules/TiledSourceModule.h"

namespace tgfx {

// The reflected format of one declared parameter kind. A kind the reflection can never produce
// has no mapping; the table builder treats that as an undeclarable kind and skips the field.
inline bool ParamTypeToUniformFormat(ParamType type, UniformFormat* format) {
  switch (type) {
    case ParamType::Float:
      *format = UniformFormat::Float;
      return true;
    case ParamType::Float2:
      *format = UniformFormat::Float2;
      return true;
    case ParamType::Float4:
    case ParamType::Float4Array:
      *format = UniformFormat::Float4;
      return true;
    case ParamType::Int:
      *format = UniformFormat::Int;
      return true;
    case ParamType::Int4:
      *format = UniformFormat::Int4;
      return true;
    case ParamType::Float3x3:
      *format = UniformFormat::Float3x3;
      return true;
    case ParamType::Float4x4:
      *format = UniformFormat::Float4x4;
      return true;
  }
  return false;
}

// Every declared field table, paired with its length. A module whose Fields are missing from
// this list is invisible to the reconciliation, so the fields its shader declares would fail the
// name check — the omission is self-reporting. conflicts, when non-null, collects every name
// declared more than once with different formats: the format table keeps the first entry, and
// without the list such a conflict would pass silently whenever the reflection happens to match
// the entry the table kept.
inline std::map<std::string, UniformFormat> BuildParamFormatTable(
    std::set<std::string>* conflicts) {
  static const std::pair<const ParamInfo*, size_t> tables[] = {
      {EngineParams::Fields, std::size(EngineParams::Fields)},
      {ChainParams::Fields, std::size(ChainParams::Fields)},
      {BlendParams::Fields, std::size(BlendParams::Fields)},
      {ColorSpaceParams::Fields, std::size(ColorSpaceParams::Fields)},
      {ParamCatalog::Fields, std::size(ParamCatalog::Fields)},
      {AnalyticClipModule::Fields, std::size(AnalyticClipModule::Fields)},
      {BlurKernelModule::Fields, std::size(BlurKernelModule::Fields)},
      {DeviceMaskModule::Fields, std::size(DeviceMaskModule::Fields)},
      {GradientModule::Fields, std::size(GradientModule::Fields)},
      {PerlinNoiseModule::Fields, std::size(PerlinNoiseModule::Fields)},
      {PointwiseOpModule::Fields, std::size(PointwiseOpModule::Fields)},
      {PorterDuffXPModule::Fields, std::size(PorterDuffXPModule::Fields)},
      {TextureSourceModule::Fields, std::size(TextureSourceModule::Fields)},
      {TiledSourceModule::Fields, std::size(TiledSourceModule::Fields)},
  };
  std::map<std::string, UniformFormat> result;
  for (const auto& table : tables) {
    for (size_t index = 0; index < table.second; ++index) {
      const auto& field = table.first[index];
      UniformFormat format = UniformFormat::Float;
      if (!ParamTypeToUniformFormat(field.type, &format)) {
        continue;
      }
      auto existing = result.find(field.name);
      if (existing == result.end()) {
        result.emplace(field.name, format);
      } else if (existing->second != format && conflicts != nullptr) {
        conflicts->insert(field.name);
      }
    }
  }
  return result;
}

inline const std::map<std::string, UniformFormat>& AllParamFormats() {
  static const std::map<std::string, UniformFormat> result = BuildParamFormatTable(nullptr);
  return result;
}

// The names BuildParamFormatTable found declared with conflicting formats (see above): the
// build-time reconciliation reports each as an error.
inline const std::set<std::string>& AllParamConflicts() {
  static const std::set<std::string> conflicts = [] {
    std::set<std::string> conflicts;
    BuildParamFormatTable(&conflicts);
    return conflicts;
  }();
  return conflicts;
}

}  // namespace tgfx
