#include "output/InlineExpansionReport.h"
#include <fstream>
#include <utility>
#include "core/DiagnosticCodes.h"

namespace tgfx {

bool WriteInlineExpansionReport(const std::string& path,
                                const std::vector<InlineExpansionRecord>& records,
                                Diagnostics* diagnostics) {
  std::ofstream file(path);
  if (!file) {
    Diagnostic diagnostic;
    diagnostic.severity = Severity::Error;
    diagnostic.layer = Layer::Output;
    diagnostic.code = DiagnosticCode::OutputWrite;
    diagnostic.message = "cannot write the inline expansion report: " + path;
    diagnostics->report(std::move(diagnostic));
    return false;
  }
  file << "{\n  \"maxDuplicatedInstructions\": " << MaxDuplicatedInstructions
       << ",\n  \"variants\": [";
  bool firstRecord = true;
  for (const auto& record : records) {
    file << (firstRecord ? "\n" : ",\n");
    firstRecord = false;
    const auto& expansion = record.expansion;
    file << "    {\"shader\": \"" << record.shaderName
         << "\", \"frag\": " << record.fragPermutationIndex
         << ", \"valid\": " << (expansion.valid ? "true" : "false")
         << ", \"ownInstructions\": " << expansion.ownInstructions
         << ", \"expandedInstructions\": " << expansion.expandedInstructions
         << ", \"duplicated\": [";
    bool firstFunction = true;
    for (const auto& function : expansion.functions) {
      if (function.copies <= 1) {
        continue;
      }
      file << (firstFunction ? "" : ", ") << "{\"name\": \"" << function.name
           << "\", \"instructions\": " << function.instructions
           << ", \"copies\": " << function.copies
           << ", \"duplicatedInstructions\": " << DuplicatedInstructions(function) << "}";
      firstFunction = false;
    }
    file << "]}";
  }
  file << "\n  ]\n}\n";
  return static_cast<bool>(file);
}

}  // namespace tgfx
