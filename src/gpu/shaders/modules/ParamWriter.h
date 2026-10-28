// The typed writer the module Binds move values through: a thin wrapper over UniformData's
// optional writes, so a field this program's variant does not declare is skipped exactly like
// setDataOptional, and the structural-suffix addressing the uniform walk maintains keeps
// multiple same-class instances (Subset / Subset_1) correct without the module knowing.
#pragma once
#include "gpu/UniformData.h"
#include "gpu/shaders/modules/ParamHandle.h"
#include "tgfx/core/Matrix.h"

namespace tgfx {

class ParamWriter {
 public:
  explicit ParamWriter(const UniformData* uniforms) : uniforms(uniforms) {
  }

  void set(const Param<ParamType::Float>& param, float value) {
    if (uniforms != nullptr) {
      uniforms->setDataOptional(param.name, value);
    }
  }

  void set(const Param<ParamType::Float2>& param, const float (&value)[2]) {
    if (uniforms != nullptr) {
      uniforms->setDataOptional(param.name, value);
    }
  }

  void set(const Param<ParamType::Float4>& param, const float (&value)[4]) {
    if (uniforms != nullptr) {
      uniforms->setDataOptional(param.name, value);
    }
  }

  void set(const Param<ParamType::Int>& param, int32_t value) {
    if (uniforms != nullptr) {
      uniforms->setDataOptional(param.name, value);
    }
  }

  void set(const Param<ParamType::Float3x3>& param, const Matrix& matrix) {
    if (uniforms != nullptr) {
      uniforms->setDataOptional(param.name, matrix);
    }
  }

  /// Whether this program's variant declares the named field. Modules use it to replicate the
  /// processors' shape-dependent branches (e.g. the device mask's decoy vs fallback writes).
  bool hasField(const char* name) const {
    return uniforms != nullptr && uniforms->hasField(name);
  }

 private:
  const UniformData* uniforms = nullptr;
};

}  // namespace tgfx
