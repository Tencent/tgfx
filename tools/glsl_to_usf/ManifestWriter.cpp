/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "ManifestWriter.h"

namespace tgfx {
namespace {

std::string JsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out.push_back(c);
    }
  }
  return out;
}

void WriteBinding(std::ostream& out, const HlslResourceBinding& b, bool first) {
  if (!first) out << ",";
  out << "{\"name\":\"" << JsonEscape(b.name) << "\""
      << ",\"stage\":\"" << b.stage << "\""
      << ",\"register\":\"" << b.regType << b.regIndex << "\"}";
}

void WriteAttribute(std::ostream& out, const HlslAttributeRemap& a, bool first) {
  if (!first) out << ",";
  out << "{\"location\":" << a.location << ",\"semantic\":\"" << JsonEscape(a.semantic) << "\"}";
}

}  // namespace

ManifestWriter::ManifestWriter(std::ostream& out) : out_(out) {
}

void ManifestWriter::beginManifest() {
  out_ << "{\"schemaVersion\":1,\"programs\":[";
}

void ManifestWriter::appendEntry(const ManifestEntry& entry) {
  if (entriesWritten_ > 0) {
    out_ << ",";
  }
  out_ << "{";
  out_ << "\"keyHex\":\"" << JsonEscape(entry.keyHex) << "\"";
  out_ << ",\"vsFile\":\"" << JsonEscape(entry.vsFile) << "\"";
  out_ << ",\"psFile\":\"" << JsonEscape(entry.psFile) << "\"";
  out_ << ",\"tuple\":{";
  out_ << "\"geometryProcessor\":\"" << JsonEscape(entry.geometryProcessor) << "\"";
  out_ << ",\"fragmentProcessors\":[";
  for (size_t i = 0; i < entry.fragmentProcessors.size(); ++i) {
    if (i > 0) out_ << ",";
    out_ << "\"" << JsonEscape(entry.fragmentProcessors[i]) << "\"";
  }
  out_ << "]";
  out_ << ",\"numColorProcessors\":" << entry.numColorProcessors;
  out_ << ",\"xferProcessor\":\"" << JsonEscape(entry.xferProcessor) << "\"";
  out_ << "}";
  out_ << ",\"bindings\":{";
  out_ << "\"cbuffers\":[";
  for (size_t i = 0; i < entry.cbuffers.size(); ++i) WriteBinding(out_, entry.cbuffers[i], i == 0);
  out_ << "],\"srvs\":[";
  for (size_t i = 0; i < entry.srvs.size(); ++i) WriteBinding(out_, entry.srvs[i], i == 0);
  out_ << "],\"samplers\":[";
  for (size_t i = 0; i < entry.samplers.size(); ++i) WriteBinding(out_, entry.samplers[i], i == 0);
  out_ << "],\"attributes\":[";
  for (size_t i = 0; i < entry.attributes.size(); ++i)
    WriteAttribute(out_, entry.attributes[i], i == 0);
  out_ << "]";
  out_ << "}";
  out_ << "}";
  ++entriesWritten_;
}

void ManifestWriter::endManifest() {
  out_ << "]}\n";
}

}  // namespace tgfx
