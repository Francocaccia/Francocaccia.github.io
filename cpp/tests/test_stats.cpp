#include "fc/api.hpp"
#include "fc/json.hpp"
#include "fc/stats.hpp"
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

const Json* tile(const Json& result, const char* id) {
  for (const Json& t : result.at("tiles").items()) {
    if (t.at("id").asString() == id) return &t;
  }
  return nullptr;
}

// The shape /stats returns, including fields the page must never use.
const char* kLive = R"({"start_date":"2026-10-13","days_tracked":3,"bot_return_pct":-2.4,"spy_return_pct":-0.8,
  "excess_return_pct":-1.6,"max_drawdown_pct":-2.48,"benchmark_60_40_return_pct":-0.16,"bot_vs_60_40_pct":-2.24,
  "risk_adjusted":"insufficient data","allocation_pct":{"equities":28.24,"bonds":0,"cash":71.76},
  "num_trades":14,"win_rate_pct":0,"open_positions_count":7,"last_updated":"2026-10-16T22:23:56Z"})";

}  // namespace

TEST(stats_formats_live_values) {
  Json r = fc::normalizeStats(parse(kLive), "2026-10-13");
  CHECK_EQ(r.at("tiles").items().size(), std::size_t(4));
  CHECK_EQ(tile(r, "return")->at("text").asString(), std::string("\xE2\x88\x92" "2.40%"));
  CHECK_EQ(tile(r, "vs_spy")->at("text").asString(), std::string("\xE2\x88\x92" "1.60%"));
  CHECK_EQ(tile(r, "vs_6040")->at("text").asString(), std::string("\xE2\x88\x92" "2.24%"));
  CHECK_EQ(tile(r, "max_drawdown")->at("text").asString(), std::string("\xE2\x88\x92" "2.48%"));
  CHECK_EQ(tile(r, "return")->at("tone").asString(), std::string("neg"));
  CHECK_EQ(tile(r, "max_drawdown")->at("tone").asString(), std::string("neutral"));
  CHECK_EQ(tile(r, "return")->at("caption").asString(), std::string("since 2026-10-13, 3 trading days"));
  CHECK_EQ(r.at("asOf").asString(), std::string("2026-10-16 22:23 UTC"));
  CHECK(r.at("hasData").asBool());
}

TEST(stats_warns_about_small_samples) {
  Json r = fc::normalizeStats(parse(kLive), "2026-10-13");
  CHECK(r.at("note").asString().find("Only 3 trading days") != std::string::npos);
  Json mature = parse(kLive);
  mature.set("days_tracked", 40);
  CHECK_EQ(fc::normalizeStats(mature, "2026-10-13").at("note").asString(), std::string(""));
}

TEST(stats_positive_values_have_a_plus_sign) {
  Json raw = parse(kLive);
  raw.set("bot_return_pct", 1.839);
  Json r = fc::normalizeStats(raw, "2026-10-13");
  CHECK_EQ(tile(r, "return")->at("text").asString(), std::string("+1.84%"));
  CHECK_EQ(tile(r, "return")->at("tone").asString(), std::string("pos"));
}

TEST(stats_null_metrics_show_a_dash_and_tracking_caption) {
  Json r = fc::normalizeStats(
      parse(R"({"start_date":"2026-10-13","days_tracked":1,"bot_return_pct":null,"excess_return_pct":null,
                "note":"insufficient data"})"),
      "2026-10-13");
  CHECK(!r.at("hasData").asBool());
  for (const Json& t : r.at("tiles").items()) {
    CHECK_EQ(t.at("text").asString(), std::string("\xE2\x80\x94"));
    CHECK(t.at("value").isNull());
    CHECK_EQ(t.at("tone").asString(), std::string("none"));
    CHECK_EQ(t.at("caption").asString(), std::string("tracking since 2026-10-13"));
  }
  CHECK_EQ(r.at("note").asString(), std::string(""));
}

TEST(stats_never_leaks_other_fields) {
  Json r = fc::normalizeStats(parse(kLive), "2026-10-13");
  std::string out = r.dump();
  for (const char* secret : {"allocation", "equities", "num_trades", "open_positions", "win_rate", "71.76", "28.24"}) {
    CHECK(out.find(secret) == std::string::npos);
  }
}

TEST(stats_rejects_implausible_values_and_bad_dates) {
  Json raw = parse(kLive);
  raw.set("bot_return_pct", 102345.12);  // looks like dollars, not a percentage
  raw.set("start_date", "<img src=x>");
  raw.set("last_updated", "not a date");
  Json r = fc::normalizeStats(raw, "2026-10-13");
  CHECK_EQ(tile(r, "return")->at("text").asString(), std::string("\xE2\x80\x94"));
  CHECK_EQ(r.at("startDate").asString(), std::string("2026-10-13"));
  CHECK_EQ(r.at("asOf").asString(), std::string(""));
}

TEST(stats_api_requires_an_object) {
  Json bad = parse(fc::call("stats.normalize", R"({"stats":"nope"})"));
  CHECK(!bad.at("ok").asBool());
  Json ok = parse(fc::call("stats.normalize", R"({"stats":{},"trackingSince":"2026-10-13"})"));
  CHECK(ok.at("ok").asBool());
  CHECK(!ok.at("result").at("hasData").asBool());
}

TEST(stats_values_that_round_to_zero_are_neutral_and_unsigned) {
  for (double v : {-0.004, 0.004, 0.0, -0.0}) {
    Json raw = parse(kLive);
    raw.set("bot_return_pct", v);
    raw.set("excess_return_pct", v);
    Json r = fc::normalizeStats(raw, "2026-10-13");
    for (const char* id : {"return", "vs_spy"}) {
      CHECK_EQ(tile(r, id)->at("text").asString(), std::string("0.00%"));
      CHECK_EQ(tile(r, id)->at("tone").asString(), std::string("neutral"));
    }
  }
  // The first value that does print as a number keeps its sign and tone.
  Json raw = parse(kLive);
  raw.set("bot_return_pct", -0.006);
  raw.set("excess_return_pct", 0.006);
  Json r = fc::normalizeStats(raw, "2026-10-13");
  CHECK_EQ(tile(r, "return")->at("text").asString(), std::string("\xE2\x88\x92" "0.01%"));
  CHECK_EQ(tile(r, "return")->at("tone").asString(), std::string("neg"));
  CHECK_EQ(tile(r, "vs_spy")->at("text").asString(), std::string("+0.01%"));
  CHECK_EQ(tile(r, "vs_spy")->at("tone").asString(), std::string("pos"));
}
