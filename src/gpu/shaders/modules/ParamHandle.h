// The typed parameter handles of the feature modules: one declaration per field, and a wrong
// name or value type is a compile error at the writer call. Deliberately free of runtime
// types, so a family header may include it (the shader build tool compiles those headers).
#pragma once
#include <cstdint>

namespace tgfx {

class FragmentProcessor;
class ParamWriter;
class ProgramInfo;

// The value kinds a module parameter can carry; the kind pins the writer overload.
enum class ParamType : uint8_t {
  Float,
  Float2,
  Float4,
  Int,
};

// A typed reference to one field of a module's parameters: the field's name in the reflected
// uniform block. Declared as a constexpr member of the module, so the name and the value type
// live in one place.
template <ParamType Type>
struct Param {
  const char* name;
};

// One module's value binding: moves a processor's values into the writer. The uniform block's
// current structural suffix applies, so a second same-class instance addresses its own fields.
using ModuleBindFn = void (*)(const FragmentProcessor*, ParamWriter*);

// Resolves a draw's processor to the module Bind that takes over its uniform writes, or null
// to keep the processor's own onSetData. Declared by a migrated family as a static member.
using ModuleBindSelector = ModuleBindFn (*)(const ProgramInfo*, const FragmentProcessor*);

}  // namespace tgfx
