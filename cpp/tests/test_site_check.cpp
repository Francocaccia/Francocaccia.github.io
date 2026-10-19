#include "fc/api.hpp"
#include "fc/json.hpp"
#include "fc/site_check.hpp"
#include "test.hpp"

using fc::Issue;
using fc::Json;

namespace {

const char* kValidSite = R"({
  "name":"A B","location":"X","positioning":"p","openTo":"o","email":"a@b.c",
  "resume":"/r.pdf","photo":"/p.jpg","footer":"f",
  "technicalSkills":["Go","C++"],"coursework":["Finance"],
  "links":{"github":"https://g","linkedin":"https://l"},
  "projects":[{"id":"p1","name":"P","tag":"PP","summary":"s","stack":["Go"],"status":"live","statusLabel":"Live"}],
  "experience":[{"role":"r","org":"o","start":"2026","description":"d"}],
  "education":[{"degree":"d","school":"s","period":"2026"}],
  "certifications":[{"name":"c","status":"active","track":"aviation"}],
  "languages":[{"name":"Spanish","level":"native"}],
  "skills":[{"name":"Go","group":"Languages","usedIn":["p1"]}],
  "airports":[{"icao":"KABE","name":"n","role":"r","lat":40.65,"lon":-75.44}],
  "aviation":{"tafSample":{"raw":"TAF KABE 161720Z 1618/1724 27008KT P6SM","label":"sample","monthDays":31},
              "examples":[{"label":"l","raw":"KABE 181451Z 00000KT 10SM CLR 09/M01 A3062"}],
              "meetsCode":[{"habit":"h","inFlight":"a","inCode":"b"}]},
  "weather":{"KABE":{"raw":"KABE 1","observedAt":"2026-10-16T17:51:00Z","sample":true}},
  "bot":{"trackingSince":"2026-10-13","apiBase":"https://x","statsPath":"/stats","historyPath":"/stats/history","historyLive":false,"benchmarks":["SPY"],
         "timeoutMs":75000,
         "policyBands":[{"label":"Equities","min":40,"max":75}],
         "riskControls":["Long-only."],
         "exampleDecision":{"action":"HOLD","conviction":5},
         "decisionFlow":[{"step":"Model","text":"t"}]}
})";

Json load(const char* text) {
  Json out;
  std::string error;
  bool ok = Json::parse(text, out, error);
  CHECK(ok);
  return out;
}

int count(const std::vector<Issue>& issues, Issue::Severity s) {
  int n = 0;
  for (const auto& i : issues) n += i.severity == s ? 1 : 0;
  return n;
}

}  // namespace

TEST(site_check_valid_data_is_clean) {
  auto issues = fc::checkSite(load(kValidSite), {});
  CHECK_EQ(issues.size(), std::size_t(0));
}

TEST(site_check_flags_todo_as_warning) {
  Json site = load(kValidSite);
  site.set("email", "TODO: add email");
  auto issues = fc::checkSite(site, {});
  CHECK_EQ(count(issues, Issue::Severity::Warning), 1);
  CHECK_EQ(count(issues, Issue::Severity::Error), 0);
  CHECK_EQ(issues[0].path, std::string("email"));
}

TEST(site_check_forbidden_terms_are_errors_and_not_echoed) {
  Json site = load(kValidSite);
  site.set("positioning", "Worked at SecretCo on things");
  auto issues = fc::checkSite(site, {"secretco"});
  CHECK_EQ(count(issues, Issue::Severity::Error), 1);
  CHECK(issues[0].message.find("SecretCo") == std::string::npos);
  CHECK(issues[0].message.find("secretco") == std::string::npos);
}

TEST(site_check_rejects_bad_enums_and_ranges) {
  Json site = load(kValidSite);
  site.set("projects", load(R"([{"id":"p","name":"n","tag":"t","summary":"s","stack":[],"status":"deployed","statusLabel":"x"}])"));
  site.set("airports", load(R"([{"icao":"K","name":"n","role":"r","lat":95,"lon":0}])"));
  auto issues = fc::checkSite(site, {});
  CHECK_EQ(count(issues, Issue::Severity::Error), 2);
}

TEST(site_check_blocks_positions_in_bot_data) {
  Json site = load(kValidSite);
  Json bot = site.at("bot");
  bot.set("positions", load(R"([{"symbol":"AAA"}])"));
  site.set("bot", bot);
  auto issues = fc::checkSite(site, {});
  CHECK(count(issues, Issue::Severity::Error) >= 1);
}

TEST(site_check_requires_policy_band_order) {
  Json site = load(kValidSite);
  Json bot = site.at("bot");
  bot.set("policyBands", load(R"([{"label":"x","min":60,"max":20}])"));
  site.set("bot", bot);
  CHECK_EQ(count(fc::checkSite(site, {}), Issue::Severity::Error), 1);
}

TEST(site_check_requires_risk_controls_and_sane_timeout) {
  Json site = load(kValidSite);
  Json bot = site.at("bot");
  bot.set("riskControls", load("[]"));
  bot.set("timeoutMs", 5);
  site.set("bot", bot);
  CHECK_EQ(count(fc::checkSite(site, {}), Issue::Severity::Error), 2);
}

TEST(api_version_and_errors) {
  Json v = load(fc::call("version", "").c_str());
  CHECK(v.at("ok").asBool());
  CHECK_EQ(v.at("result").at("core").asString(), std::string(fc::kCoreVersion));

  Json bad = load(fc::call("nope", "").c_str());
  CHECK(!bad.at("ok").asBool());

  Json badInput = load(fc::call("json.format", "{").c_str());
  CHECK(!badInput.at("ok").asBool());
}

TEST(api_site_check_roundtrip) {
  std::string input = std::string(R"({"data":)") + kValidSite + R"(,"forbidden":[]})";
  Json r = load(fc::call("site.check", input).c_str());
  CHECK(r.at("ok").asBool());
  CHECK_EQ(r.at("result").items().size(), std::size_t(0));
}
