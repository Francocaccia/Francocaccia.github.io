// Single entry point for everything the browser and the build call into.
// Every operation takes a JSON string and returns a JSON string, so the
// WebAssembly boundary is one function and new modules only add an `op`.
#pragma once

#include <string>

namespace fc {

inline constexpr const char* kCoreVersion = "0.1.0";

// Returns {"ok":true,"result":...} or {"ok":false,"error":"..."}.
std::string call(const std::string& op, const std::string& inputJson);

}  // namespace fc
