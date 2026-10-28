// The plan-binding registry's storage. See PlanBindingsRegistry.h for the layering rationale.
#include "gpu/shaders/params/PlanBindingsRegistry.h"
#include <unordered_map>

namespace tgfx {

namespace {
// Function-local static: the families' registration macros run during static initialization in
// arbitrary order, so the map must exist before the first Register call.
std::unordered_map<std::string, PlanBindingsRegistry::Fn>& RegistryMap() {
  static std::unordered_map<std::string, PlanBindingsRegistry::Fn> map;
  return map;
}
}  // namespace

void PlanBindingsRegistry::Register(const char* familyName, Fn fn) {
  RegistryMap()[familyName] = fn;
}

PlanBindingsRegistry::Fn PlanBindingsRegistry::Find(const std::string& familyName) {
  auto& map = RegistryMap();
  auto it = map.find(familyName);
  return it == map.end() ? nullptr : it->second;
}

}  // namespace tgfx
