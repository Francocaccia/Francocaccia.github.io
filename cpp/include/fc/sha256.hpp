// SHA-256 (FIPS 180-4), used by the toy blockchain.
#pragma once

#include <string>

namespace fc {

// Hash of the raw bytes of `bytes`, as 64 lowercase hex characters.
std::string sha256Hex(const std::string& bytes);

}  // namespace fc
