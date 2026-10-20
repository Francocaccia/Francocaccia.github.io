#include "fc/chain.hpp"

#include <cstddef>
#include <string>
#include <vector>

#include "clamp.hpp"
#include "fc/sha256.hpp"

namespace fc {

namespace {

constexpr long long kMaxTries = 5000000;

struct Block {
  int index = 0;
  std::string data, prev;
  long long nonce = 0;
  std::string hash;
};

std::string preimage(int index, const std::string& prev, const std::string& data, long long nonce) {
  return std::to_string(index) + "|" + prev + "|" + data + "|" + std::to_string(nonce);
}

bool meets(const std::string& hash, int difficulty) {
  auto zeros = static_cast<std::size_t>(difficulty);
  return hash.compare(0, zeros, std::string(zeros, '0')) == 0;
}

// Drops control characters (including DEL), then keeps at most 64 characters. UTF-8 sequences are counted by
// their lead byte so a multi-byte character is never cut in half.
std::string cleanData(const std::string& raw) {
  std::string out;
  std::size_t chars = 0;
  for (char ch : raw) {
    auto c = static_cast<unsigned char>(ch);
    if (c < 0x20 || c == 0x7f) continue;
    bool continuation = (c & 0xC0) == 0x80;
    if (!continuation) {
      if (chars == kChainMaxData) break;
      ++chars;
    }
    out += ch;
  }
  return out;
}

// The block's text with a trailing "!". A full 64-character text gives up its last character so the change stays
// within the limit (and is still a change: a text already ending in "!" gets "?" instead).
std::string tamperedData(const std::string& data) {
  std::size_t chars = 0;
  for (char ch : data) chars += (static_cast<unsigned char>(ch) & 0xC0) != 0x80;
  std::string base = data;
  bool full = chars >= kChainMaxData;
  if (full) {
    while ((static_cast<unsigned char>(base.back()) & 0xC0) == 0x80) base.pop_back();  // continuation bytes
    base.pop_back();                                                                      // and the lead byte
  }
  return base + (full && data.back() == '!' ? "?" : "!");
}

Json blockJson(const Block& b) {
  Json j = Json::object();
  j.set("index", b.index);
  j.set("data", b.data);
  j.set("prev", b.prev);
  j.set("nonce", static_cast<double>(b.nonce));
  j.set("hash", b.hash);
  return j;
}

}  // namespace

bool buildChain(const Json& in, Json& out, std::string& error) {
  std::vector<std::string> data;
  for (const Json& d : in.at("data").items()) {
    if (d.isString() && data.size() < static_cast<std::size_t>(kChainMaxBlocks)) data.push_back(cleanData(d.asString()));
  }
  if (data.empty()) data = {"Genesis block", "A pays B 5", "B pays C 2", "C pays A 1", "A pays D 3"};
  int difficulty = static_cast<int>(clampNumber(in.at("difficulty").asNumber(3), 1, 4, 3));

  std::vector<Block> chain;
  std::string prev(64, '0');
  for (std::size_t i = 0; i < data.size(); ++i) {
    Block b;
    b.index = static_cast<int>(i);
    b.data = data[i];
    b.prev = prev;
    for (;; ++b.nonce) {
      if (b.nonce >= kMaxTries) {
        error = "chain.demo: no valid nonce for block " + std::to_string(i) + " within " + std::to_string(kMaxTries) + " tries";
        return false;
      }
      b.hash = sha256Hex(preimage(b.index, b.prev, b.data, b.nonce));
      if (meets(b.hash, difficulty)) break;
    }
    prev = b.hash;
    chain.push_back(b);
  }

  Json blocks = Json::array();
  for (const Block& b : chain) blocks.push(blockJson(b));

  // Change one block's data and leave every stored prev and nonce alone: only the hashes are recomputed.
  Json tamper = Json::array();
  for (std::size_t i = 0; i < chain.size(); ++i) {
    std::string changed = tamperedData(chain[i].data);
    Json entry = Json::object();
    entry.set("block", static_cast<int>(i));
    entry.set("data", changed);
    Json list = Json::array();
    std::string before(64, '0');  // recomputed hash of the previous block
    for (std::size_t k = 0; k < chain.size(); ++k) {
      Block b = chain[k];
      if (k == i) b.data = changed;
      b.hash = sha256Hex(preimage(b.index, b.prev, b.data, b.nonce));
      Json j = blockJson(b);
      j.set("meetsDifficulty", meets(b.hash, difficulty));
      j.set("linked", b.prev == before);
      list.push(j);
      before = b.hash;
    }
    entry.set("blocks", list);
    tamper.push(entry);
  }

  out = Json::object();
  out.set("algorithm", "SHA-256");
  out.set("difficulty", difficulty);
  out.set("format", "index|prev|data|nonce");
  out.set("blocks", blocks);
  out.set("tamper", tamper);
  return true;
}

}  // namespace fc
