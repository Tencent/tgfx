// The registry of the plan-binding declarations migrated kernel families register from their
// own translation units. The shader build tool compiles the family HEADERS (its templates
// instantiate there) but never links the family .cpps, so the plan hook cannot ride a virtual
// on the family class — a header reference to a .cpp symbol would fail the tool's link. The
// registration macro below lives in the family .cpp (runtime side), and the program creation
// looks the hook up by the match result's family name, exactly like the shader registry the
// tool itself uses.
#pragma once
#include <string>

namespace tgfx {

class BindingPlanBuilder;
class ProgramInfo;

class PlanBindingsRegistry {
 public:
  // Declares the parameter module instances of a draw; returns false to keep the processors'
  // own onSetData writes (un-migrated shapes, or a family that never migrated).
  using Fn = bool (*)(const ProgramInfo*, BindingPlanBuilder*);

  static void Register(const char* familyName, Fn fn);

  // The registered hook, or null when the family has not migrated.
  static Fn Find(const std::string& familyName);
};

#define TGFX_REGISTER_PLAN_BINDINGS_IMPL(ShaderClass, counter)                               \
  namespace {                                                                                \
  struct TGFX_CONCAT(PlanBindingsAutoReg_, counter) {                                        \
    TGFX_CONCAT(PlanBindingsAutoReg_, counter)() {                                           \
      ::tgfx::PlanBindingsRegistry::Register(ShaderClass::Name, &ShaderClass::PlanBindings); \
    }                                                                                        \
  };                                                                                         \
  static TGFX_CONCAT(PlanBindingsAutoReg_, counter)                                          \
      TGFX_CONCAT(planBindingsAutoRegInstance_, counter){};                                  \
  }

/// Registers a family's static PlanBindings(const ProgramInfo*, BindingPlanBuilder*) as its
/// plan hook. Place in the family's .cpp; the tool never links that file.
#define TGFX_REGISTER_PLAN_BINDINGS(ShaderClass) \
  TGFX_REGISTER_PLAN_BINDINGS_IMPL(ShaderClass, __COUNTER__)

}  // namespace tgfx
