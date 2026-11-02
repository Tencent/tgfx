// The typed writer the module Binds move values through: a thin wrapper over UniformData's
// optional writes, so a field this program's variant does not declare is skipped exactly like
// setDataOptional, and the structural instance addressing the uniform walk maintains keeps
// multiple same-class instances (Subset / Subset_1) correct without the module knowing. On a
// program with a resolved parameter layout the writes go through the offset path (one pointer
// lookup, no name composition); the name path stays as the authority every miss falls back to.
#pragma once
#include <string>
#include <type_traits>
#include "gpu/UniformData.h"
#include "gpu/shaders/modules/ParamHandle.h"
#include "tgfx/core/Matrix.h"

namespace tgfx {

class ParamWriter {
 public:
  explicit ParamWriter(const UniformData* uniforms) : uniforms(uniforms) {
  }

  /// The structural instance the next writes address (Subset / Subset_1 / ...): the offset
  /// path takes it as the slot's instance index, and the name-path fallback composes it into
  /// the field name, so the two paths address the same field.
  void setInstanceOrdinal(size_t ordinal) {
    instanceOrdinal = ordinal;
  }

  void set(const Param<ParamType::Float>& param, float value) {
    if (uniforms != nullptr) {
      if (uniforms->writeParamKey(&param, instanceOrdinal, &value, sizeof(value))) {
        return;
      }
      uniforms->setDataOptional(OrdinalName(param.name), value);
    }
  }

  void set(const Param<ParamType::Float2>& param, const float (&value)[2]) {
    if (uniforms != nullptr) {
      if (uniforms->writeParamKey(&param, instanceOrdinal, value, sizeof(value))) {
        return;
      }
      uniforms->setDataOptional(OrdinalName(param.name), value);
    }
  }

  void set(const Param<ParamType::Float4>& param, const float (&value)[4]) {
    if (uniforms != nullptr) {
      if (uniforms->writeParamKey(&param, instanceOrdinal, value, sizeof(value))) {
        return;
      }
      uniforms->setDataOptional(OrdinalName(param.name), value);
    }
  }

  void set(const Param<ParamType::Int>& param, int32_t value) {
    if (uniforms != nullptr) {
      if (uniforms->writeParamKey(&param, instanceOrdinal, &value, sizeof(value))) {
        return;
      }
      uniforms->setDataOptional(OrdinalName(param.name), value);
    }
  }

  void set(const Param<ParamType::Float3x3>& param, const Matrix& matrix) {
    if (uniforms != nullptr) {
      if (uniforms->writeParamKey(&param, instanceOrdinal, matrix)) {
        return;
      }
      uniforms->setDataOptional(OrdinalName(param.name), matrix);
    }
  }

  void set(const Param<ParamType::Float3x3>& param, const std::array<float, 12>& aligned) {
    if (uniforms != nullptr) {
      if (uniforms->writeParamKey(&param, instanceOrdinal, aligned.data(), sizeof(aligned))) {
        return;
      }
      uniforms->setDataOptional(OrdinalName(param.name), aligned);
    }
  }

  template <size_t N>
  void set(const Param<ParamType::Float4Array>& param, const std::array<float, N>& values) {
    // The table kinds write whole arrays of varying length; they keep the name path.
    if (uniforms != nullptr) {
      uniforms->setDataOptional(OrdinalName(param.name), values);
    }
  }

  template <ParamType Type, typename T>
  void setArrayElement(const Param<Type>& param, size_t index, const T& value) {
    if (uniforms != nullptr) {
      if constexpr (!std::is_same_v<std::decay_t<T>, Matrix>) {
        // Only the ordinal-0 instance accelerates: a later ordinal's fields live under an
        // ordinal-qualified name ("ColorMatrix_1"), a second addressing axis the single
        // (instance) form of the offset path does not carry.
        if (instanceOrdinal == 0) {
          if (uniforms->writeParamKey(&param, index, &value, sizeof(value))) {
            return;
          }
        }
      }
      uniforms->setArrayElementOptional(OrdinalName(param.name), index, value);
    }
  }

  /// Whether this program's variant declares the named field. Modules use it to replicate the
  /// processors' shape-dependent branches (e.g. the device mask's decoy vs fallback writes).
  bool hasField(const char* name) const {
    return uniforms != nullptr && uniforms->hasField(name);
  }

  /// The raw uniform block, for delegating to shared native writers whose per-element layout
  /// (e.g. the pointwise slot records) already lives in one place.
  const UniformData* rawData() const {
    return uniforms;
  }

 private:
  /// The fallback name the instance ordinal implies: instance 0 keeps the base name, later
  /// instances append their ordinal ("Subset_1") -- the addressing the kernel's fields declare.
  std::string OrdinalName(const char* name) const {
    return instanceOrdinal == 0 ? std::string(name)
                                : std::string(name) + "_" + std::to_string(instanceOrdinal);
  }

  const UniformData* uniforms = nullptr;
  size_t instanceOrdinal = 0;
};

}  // namespace tgfx
