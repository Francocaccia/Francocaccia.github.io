// WebAssembly boundary: two C functions, strings in and out.
#include <cstdlib>
#include <cstring>
#include <string>

#include <emscripten/emscripten.h>

#include "fc/api.hpp"

extern "C" {

// Returns a heap string the caller must release with fc_free().
EMSCRIPTEN_KEEPALIVE
char* fc_call(const char* op, const char* inputJson) {
  std::string out = fc::call(op ? op : "", inputJson ? inputJson : "");
  char* copy = static_cast<char*>(std::malloc(out.size() + 1));
  if (!copy) return nullptr;
  std::memcpy(copy, out.c_str(), out.size() + 1);
  return copy;
}

EMSCRIPTEN_KEEPALIVE
void fc_free(char* p) { std::free(p); }

}  // extern "C"
