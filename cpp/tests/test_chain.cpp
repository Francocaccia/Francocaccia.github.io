#include <cstddef>
#include <string>

#include "fc/api.hpp"
#include "fc/chain.hpp"
#include "fc/json.hpp"
#include "fc/sha256.hpp"
#include "test.hpp"

using fc::Json;

namespace {

Json parse(const std::string& text) {
  Json out;
  std::string error;
  bool ok = Json::parse(text, out, error);
  CHECK(ok);
  return out;
}

// Runs chain.demo and returns the whole envelope.
Json demo(const std::string& input = "{}") { return parse(fc::call("chain.demo", input)); }

std::string preimage(const Json& b) {
  return std::to_string(static_cast<int>(b.at("index").asNumber())) + "|" + b.at("prev").asString() + "|" +
         b.at("data").asString() + "|" + std::to_string(static_cast<long long>(b.at("nonce").asNumber()));
}

bool startsWithZeros(const std::string& hash, int n) { return hash.compare(0, static_cast<std::size_t>(n), std::string(static_cast<std::size_t>(n), '0')) == 0; }

}  // namespace

TEST(sha256_matches_the_fips_vectors) {
  CHECK_EQ(fc::sha256Hex(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
  CHECK_EQ(fc::sha256Hex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
  // 448 bits: the padding no longer fits in the same block.
  CHECK_EQ(fc::sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
           std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
}

TEST(sha256_handles_the_padding_boundaries) {
  // Expected values come from Node's crypto module.
  CHECK_EQ(fc::sha256Hex(std::string(55, 'a')), std::string("9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"));
  CHECK_EQ(fc::sha256Hex(std::string(56, 'a')), std::string("b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"));
  CHECK_EQ(fc::sha256Hex(std::string(63, 'a')), std::string("7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"));
  CHECK_EQ(fc::sha256Hex(std::string(64, 'a')), std::string("ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"));
  CHECK_EQ(fc::sha256Hex(std::string(65, 'a')), std::string("635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"));
}

TEST(hash_sha256_op_and_its_size_limit) {
  Json ok = parse(fc::call("hash.sha256", R"({"text":"abc"})"));
  CHECK(ok.at("ok").asBool());
  CHECK_EQ(ok.at("result").at("hex").asString(), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

  // A missing text hashes the empty string.
  CHECK_EQ(parse(fc::call("hash.sha256", "{}")).at("result").at("hex").asString(),
           std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));

  Json atLimit = parse(fc::call("hash.sha256", R"({"text":")" + std::string(4096, 'a') + R"("})"));
  CHECK(atLimit.at("ok").asBool());
  Json over = parse(fc::call("hash.sha256", R"({"text":")" + std::string(4097, 'a') + R"("})"));
  CHECK(!over.at("ok").asBool());
  CHECK(over.at("error").isString());
}

TEST(chain_demo_is_deterministic) {
  std::string a = fc::call("chain.demo", "{}");
  CHECK_EQ(a, fc::call("chain.demo", "{}"));
  CHECK_EQ(a, fc::call("chain.demo", ""));
  CHECK(a != fc::call("chain.demo", R"({"difficulty":2})"));
}

TEST(chain_demo_defaults_and_shape) {
  Json r = demo().at("result");
  CHECK_EQ(r.at("algorithm").asString(), std::string("SHA-256"));
  CHECK_EQ(r.at("difficulty").asNumber(), 3.0);
  CHECK_EQ(r.at("format").asString(), std::string("index|prev|data|nonce"));
  CHECK_EQ(r.at("blocks").items().size(), std::size_t(5));
  CHECK_EQ(r.at("tamper").items().size(), std::size_t(5));
  CHECK_EQ(r.at("blocks").at(std::size_t(0)).at("data").asString(), std::string("Genesis block"));
  CHECK_EQ(r.at("blocks").at(4).at("data").asString(), std::string("A pays D 3"));
  // Nonces and hashes of the default chain, cross-checked against Node's crypto module.
  CHECK_EQ(r.at("blocks").at(std::size_t(0)).at("nonce").asNumber(), 125.0);
  CHECK_EQ(r.at("blocks").at(std::size_t(0)).at("hash").asString(), std::string("0004dc2e5e707dd43a12b2009ed3f1baa336c95c8977ed5d650350d5cbc7acba"));
  CHECK_EQ(r.at("blocks").at(4).at("nonce").asNumber(), 1510.0);
  CHECK_EQ(r.at("blocks").at(4).at("hash").asString(), std::string("000f4286f22bd08fbb86d2335d2d56ec01eb5ec20979bce78c013d008ea8f777"));
}

TEST(chain_demo_blocks_are_mined_and_linked) {
  for (int difficulty = 1; difficulty <= 4; ++difficulty) {
    Json r = demo(R"({"difficulty":)" + std::to_string(difficulty) + "}").at("result");
    CHECK_EQ(r.at("difficulty").asNumber(), double(difficulty));
    std::string prev(64, '0');
    const auto& blocks = r.at("blocks").items();
    CHECK_EQ(blocks.size(), std::size_t(5));
    for (std::size_t i = 0; i < blocks.size(); ++i) {
      const Json& b = blocks[i];
      CHECK_EQ(b.at("index").asNumber(), double(i));
      CHECK_EQ(b.at("prev").asString(), prev);
      CHECK(startsWithZeros(b.at("hash").asString(), difficulty));
      CHECK_EQ(b.at("hash").asString(), fc::sha256Hex(preimage(b)));
      prev = b.at("hash").asString();
    }
  }
}

TEST(chain_demo_tamper_breaks_the_changed_block_and_the_link_after_it) {
  Json r = demo().at("result");
  const auto& blocks = r.at("blocks").items();
  const auto& tamper = r.at("tamper").items();
  CHECK_EQ(tamper.size(), blocks.size());
  for (std::size_t i = 0; i < tamper.size(); ++i) {
    CHECK_EQ(tamper[i].at("block").asNumber(), double(i));
    CHECK_EQ(tamper[i].at("data").asString(), blocks[i].at("data").asString() + "!");
    const auto& view = tamper[i].at("blocks").items();
    CHECK_EQ(view.size(), blocks.size());
    for (std::size_t k = 0; k < view.size(); ++k) {
      // Stored fields stay put; only the changed block's data differs.
      CHECK_EQ(view[k].at("prev").asString(), blocks[k].at("prev").asString());
      CHECK_EQ(view[k].at("nonce").asNumber(), blocks[k].at("nonce").asNumber());
      CHECK_EQ(view[k].at("data").asString(), k == i ? tamper[i].at("data").asString() : blocks[k].at("data").asString());
      CHECK_EQ(view[k].at("hash").asString(), fc::sha256Hex(preimage(view[k])));
      CHECK_EQ(view[k].at("meetsDifficulty").asBool(), startsWithZeros(view[k].at("hash").asString(), 3));

      if (k < i) {
        CHECK(view[k].at("meetsDifficulty").asBool());
        CHECK(view[k].at("linked").asBool());
        CHECK_EQ(view[k].at("hash").asString(), blocks[k].at("hash").asString());
      } else if (k == i) {
        // The changed block's own link is still good; its hash is what moved.
        CHECK(view[k].at("linked").asBool());
        CHECK(view[k].at("hash").asString() != blocks[k].at("hash").asString());
        CHECK(!view[k].at("meetsDifficulty").asBool());  // fails for the default data; a 1-in-4096 chance otherwise
      } else if (k == i + 1) {
        // The stored prev still names the old hash of block i, which no longer matches.
        CHECK(!view[k].at("linked").asBool());
      } else {
        // Later links are judged one step at a time: block k still names block k-1's untouched hash. The break
        // is one link wide, which is why a validator rejects the chain at the first failure.
        CHECK(view[k].at("linked").asBool());
      }
    }
  }
}

TEST(chain_demo_clamps_difficulty_and_block_count) {
  CHECK_EQ(demo(R"({"difficulty":99})").at("result").at("difficulty").asNumber(), 4.0);
  CHECK_EQ(demo(R"({"difficulty":-5})").at("result").at("difficulty").asNumber(), 1.0);
  CHECK_EQ(demo(R"({"difficulty":0})").at("result").at("difficulty").asNumber(), 1.0);
  CHECK_EQ(demo(R"({"difficulty":"hard"})").at("result").at("difficulty").asNumber(), 3.0);
  Json four = demo(R"({"difficulty":99})").at("result");
  for (const Json& b : four.at("blocks").items()) CHECK(startsWithZeros(b.at("hash").asString(), 4));

  // More than eight blocks are truncated to the first eight rather than rejected.
  std::string many = R"({"difficulty":1,"data":[)";
  for (int i = 0; i < 100; ++i) many += std::string(i ? "," : "") + "\"tx " + std::to_string(i) + "\"";
  many += "]}";
  Json r = demo(many);
  CHECK(r.at("ok").asBool());
  CHECK_EQ(r.at("result").at("blocks").items().size(), std::size_t(8));
  CHECK_EQ(r.at("result").at("tamper").items().size(), std::size_t(8));
  CHECK_EQ(r.at("result").at("blocks").at(7).at("data").asString(), std::string("tx 7"));

  // An empty or malformed list falls back to the default chain.
  CHECK_EQ(demo(R"({"data":[]})").at("result").at("blocks").items().size(), std::size_t(5));
  CHECK_EQ(demo(R"({"data":"nope"})").at("result").at("blocks").items().size(), std::size_t(5));
}

TEST(chain_demo_sanitizes_block_text) {
  // JSON escapes decode to a newline, a tab, U+0001 and DEL; all are dropped.
  Json r = demo(R"({"difficulty":1,"data":["a\nb\tc\u0001d\u007fe", "x"]})").at("result");
  CHECK_EQ(r.at("blocks").at(std::size_t(0)).at("data").asString(), std::string("abcde"));
  CHECK_EQ(r.at("blocks").at(1).at("data").asString(), std::string("x"));

  // Long text is cut to 64 characters, counting a multi-byte character once.
  Json cut = demo(R"({"difficulty":1,"data":[")" + std::string(100, 'z') + R"("]})").at("result");
  CHECK_EQ(cut.at("blocks").at(std::size_t(0)).at("data").asString(), std::string(64, 'z'));
  CHECK_EQ(cut.at("tamper").at(std::size_t(0)).at("data").asString().size(), std::size_t(64));
  CHECK(cut.at("tamper").at(std::size_t(0)).at("data").asString() != cut.at("blocks").at(std::size_t(0)).at("data").asString());
  CHECK_EQ(cut.at("tamper").at(std::size_t(0)).at("data").asString(), std::string(63, 'z') + "!");

  std::string accents;
  for (int i = 0; i < 70; ++i) accents += "\xc3\xa9";  // 70 x U+00E9
  Json utf = demo(R"({"difficulty":1,"data":[")" + accents + R"("]})").at("result");
  CHECK_EQ(utf.at("blocks").at(std::size_t(0)).at("data").asString().size(), std::size_t(128));  // 64 characters, 2 bytes each
  std::string changed = utf.at("tamper").at(std::size_t(0)).at("data").asString();
  CHECK_EQ(changed.size(), std::size_t(127));  // 63 characters plus "!"
  CHECK_EQ(changed.substr(126), std::string("!"));

  // A full-length text that already ends in "!" still changes.
  Json bang = demo(R"({"difficulty":1,"data":[")" + std::string(63, 'q') + R"(!"]})").at("result");
  CHECK_EQ(bang.at("tamper").at(std::size_t(0)).at("data").asString(), std::string(63, 'q') + "?");

  // A block may be empty after cleaning; the tamper entry is just "!".
  Json blank = demo(R"({"difficulty":1,"data":["\n\t"]})").at("result");
  CHECK_EQ(blank.at("blocks").at(std::size_t(0)).at("data").asString(), std::string(""));
  CHECK_EQ(blank.at("tamper").at(std::size_t(0)).at("data").asString(), std::string("!"));
}
