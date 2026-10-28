// The typed writer the module Binds move values through: a thin wrapper over UniformData's
// optional writes, so a field this program's variant does not declare is skipped exactly like
// setDataOptional, and the structural-suffix addressing the uniform walk maintains keeps
// multiple same-class instances (Subset / Subset_1) correct without the module knowing.
#pragma once
#include "gpu/UniformData.h"
#include "gpu/shaders/modules/ParamHandle.h"

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

 private:
  const UniformData* uniforms = nullptr;
};

}  // namespace tgfx
