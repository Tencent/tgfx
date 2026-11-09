// The aggregation every build-time uniform reconciliation walks, and the runtime's parameter
// identity space. One table maps each declared field name to the UniformFormat the reflected
// uniform block must carry (the tool's reconciliation); the same tables, flattened in a fixed
// order, give every parameter a stable id, so the runtime's offset resolution (AOTParamLayout)
// can enumerate them and a writer holding a Param constant can find its slot with one pointer
// lookup. A name the table does not know is a field nobody declared, and a name with the wrong
// format is a declaration that drifted from the shader.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>
#include "gpu/Uniform.h"
#include "gpu/aot/bind/ParamCatalog.h"
#include "gpu/shaders/Families.h"
#include "gpu/shaders/modules/AnalyticClipModule.h"
#include "gpu/shaders/modules/BlendParams.h"
#include "gpu/shaders/modules/BlurKernelModule.h"
#include "gpu/shaders/modules/ChainParams.h"
#include "gpu/shaders/modules/DeviceMaskModule.h"
#include "gpu/shaders/modules/EngineParams.h"
#include "gpu/shaders/modules/GradientModule.h"
#include "gpu/shaders/modules/ParamHandle.h"
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

// Every declared field table, paired with its length, in the registry's fixed order. A module
// whose Fields are missing from this list is invisible both to the reconciliation (the fields its
// shader declares would fail the name check) and to the runtime's id space (its writers keep the
// name-addressed path), so the omission is self-reporting.
inline const std::vector<std::pair<const ParamInfo*, size_t>>& ParamFieldTables() {
  static const std::vector<std::pair<const ParamInfo*, size_t>> tables = {
      {EngineParams::Fields, std::size(EngineParams::Fields)},
      {ChainParams::Fields, std::size(ChainParams::Fields)},
      {BlendParams::Fields, std::size(BlendParams::Fields)},
      {ParamCatalog::Fields, std::size(ParamCatalog::Fields)},
      {AnalyticClipModule::Fields, std::size(AnalyticClipModule::Fields)},
      {BlurKernelModule::Fields, std::size(BlurKernelModule::Fields)},
      {DeviceMaskModule::Fields, std::size(DeviceMaskModule::Fields)},
      {GradientModule::Fields, std::size(GradientModule::Fields)},
      {PorterDuffXPModule::Fields, std::size(PorterDuffXPModule::Fields)},
      {TextureSourceModule::Fields, std::size(TextureSourceModule::Fields)},
      {TiledSourceModule::Fields, std::size(TiledSourceModule::Fields)},
  };
  return tables;
}

// conflicts, when non-null, collects every name declared more than once with different formats:
// the format table keeps the first entry, and without the list such a conflict would pass
// silently whenever the reflection happens to match the entry the table kept.
inline std::map<std::string, UniformFormat> BuildParamFormatTable(
    std::set<std::string>* conflicts) {
  std::map<std::string, UniformFormat> result;
  auto declareName = [&result, conflicts](const std::string& name, UniformFormat format) {
    auto existing = result.find(name);
    if (existing == result.end()) {
      result.emplace(name, format);
    } else if (existing->second != format && conflicts != nullptr) {
      conflicts->insert(name);
    }
  };
  for (const auto& table : ParamFieldTables()) {
    for (size_t index = 0; index < table.second; ++index) {
      const auto& field = table.first[index];
      UniformFormat format = UniformFormat::Float;
      if (!ParamTypeToUniformFormat(field.type, &format)) {
        continue;
      }
      declareName(field.name, format);
    }
  }
  // The declaration-driven families join the same reconciliation: their present elements name
  // exactly what the assembled variants reflect.
  for (size_t familyIndex = 0; familyIndex < FamilyCount(); ++familyIndex) {
    const Family& family = Families()[familyIndex];
    for (const auto& variant : EnumerateVariants(family, static_cast<uint16_t>(familyIndex))) {
      const auto modules = ModulesOf(family, variant);
      for (uint8_t slot = 0; slot < modules.count; ++slot) {
        const Module* module = modules.list[slot];
        if (module == nullptr || !ModulePresent(family, variant, slot)) {
          continue;
        }
        const uint8_t dimBase = DimOffsetOf(family, variant, slot);
        for (uint8_t index = 0; index < module->elementCount; ++index) {
          const Element& element = module->elements[index];
          if (element.when >= 0) {
            const uint8_t value = variant.values[dimBase + static_cast<uint8_t>(element.when)];
            const bool present = element.whenValue >= 0
                                     ? value == static_cast<uint8_t>(element.whenValue)
                                     : value != 0;
            if (!present) {
              continue;
            }
          }
          if (element.kind != Element::Kind::Param && element.kind != Element::Kind::VertexParam) {
            continue;
          }
          declareName(ElementNameOf(module, element), ShaderTypeFormat(element.type));
        }
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

// ---- The parameter identity space --------------------------------------------------------------
//
// The tables above flattened in their fixed order, deduplicated by name (the first declaration
// wins, exactly as the format table keeps it): the index of an entry is the parameter's id, the
// one number AOTParamLayout resolves into byte addressing once per program.

inline constexpr uint16_t kNoParamId = 0xFFFF;

// Every declared parameter in the registry's stable order; the index is the parameter's id.
inline const std::vector<ParamInfo>& AllParamList() {
  static const std::vector<ParamInfo> list = [] {
    std::vector<ParamInfo> result = {};
    std::set<std::string> seen = {};
    for (const auto& table : ParamFieldTables()) {
      for (size_t index = 0; index < table.second; ++index) {
        if (seen.insert(table.first[index].name).second) {
          result.push_back(table.first[index]);
        }
      }
    }
    return result;
  }();
  return list;
}

// A Param constant's id: one pointer lookup, no string work. Every table entry's key maps to
// its name's id, so two modules declaring the same field name (the device mask's decoy Subset
// and the texture source's Subset) address the same slot; the id's name — the first declaration
// — is the one the layout resolves by. kNoParamId when the key is unknown (a ParamInfo built
// without a Param constant, or a constant missing from every Fields list), in which case the
// writer keeps the name-addressed path — correct, just not accelerated.
inline uint16_t ParamIdOf(const void* key) {
  struct KeyIndex {
    std::unordered_map<const void*, uint16_t> ids = {};
    KeyIndex() {
      // The name's id: the first declaration in table order wins, matching AllParamList.
      std::unordered_map<std::string, uint16_t> byName = {};
      const auto& list = AllParamList();
      for (size_t id = 0; id < list.size(); ++id) {
        byName.emplace(list[id].name, static_cast<uint16_t>(id));
      }
      // Every declared constant's key, including duplicate names' later declarations.
      for (const auto& table : ParamFieldTables()) {
        for (size_t index = 0; index < table.second; ++index) {
          const auto& field = table.first[index];
          if (field.key == nullptr) {
            continue;
          }
          auto it = byName.find(field.name);
          if (it != byName.end()) {
            ids.emplace(field.key, it->second);
          }
        }
      }
    }
  };
  static const KeyIndex index = {};
  auto it = index.ids.find(key);
  return it != index.ids.end() ? it->second : kNoParamId;
}

}  // namespace tgfx
