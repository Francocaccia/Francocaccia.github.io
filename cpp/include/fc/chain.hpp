// A toy blockchain for the landing page: real SHA-256, proof of work and a tamper demo, computed at build time.
#pragma once

#include <cstddef>
#include <string>

#include "fc/json.hpp"

namespace fc {

inline constexpr int kChainMaxBlocks = 8;          // extra blocks in the input are dropped
inline constexpr std::size_t kChainMaxData = 64;   // characters per block

// Input (all optional): {data?: [string], difficulty?: leading hex zeros}.
// Data is cut to 8 blocks of at most 64 characters each, with control characters removed.
// Difficulty is clamped to 1..4. Each block hashes the text `index|prev|data|nonce` (decimal index and nonce);
// the genesis prev is 64 zeros. Nonces count up from 0 until the hash has `difficulty` leading zeros.
// Output: {algorithm, difficulty, format, blocks:[{index,data,prev,nonce,hash}], tamper:[{block,data,blocks:[
//   {index,data,prev,nonce,hash,meetsDifficulty,linked}]}]}.
// tamper[i] is the chain after block i's data gains a trailing "!" and nothing is re-mined. A text already at
// 64 characters swaps its last character for the "!" (or "?" if that character is "!") so it stays within the limit.
// Returns false and sets `error` when a block cannot be mined within the try limit.
bool buildChain(const Json& in, Json& out, std::string& error);

}  // namespace fc
