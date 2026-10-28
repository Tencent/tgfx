// The typed parameter handles of the feature modules: one declaration per field, and a wrong
// name or value type is a compile error at the writer call. Deliberately free of runtime
// types, so a family header may include it (the shader build tool compiles those headers).
#pragma once
#include <cstdint>

namespace tgfx {

class ParamWriter;

// The value kinds a module parameter can carry; the kind pins the writer overload.
enum class ParamType : uint8_t {
  Float,
  Float2,
  Float4,
  Int,
  Float3x3,
  // A float table declared as an array of vec4 (e.g. the blur half-kernel).
  Float4Array,
  // One element of a mat4 array (e.g. the per-slot color matrix).
  Float4x4,
};

// A typed reference to one field of a module's parameters: the field's name in the reflected
// uniform block. Declared as a constexpr member of the module, so the name and the value type
// live in one place.
template <ParamType Type>
struct Param {
  const char* name;
};

}  // namespace tgfx
