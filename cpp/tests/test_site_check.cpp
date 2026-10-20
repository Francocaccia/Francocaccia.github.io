#include <functional>

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
  "experience":[{"id":"e1","role":"r","org":"o","start":"2026","end":"present","description":"d"}],
  "education":[{"degree":"d","school":"s","period":"2026"}],
  "certifications":[{"name":"c","status":"active","track":"aviation"}],
  "languages":[{"name":"Spanish","level":"native"}],
  "skills":[{"name":"Go","group":"Languages","usedIn":["p1"]}],
  "airports":[{"icao":"KABE","name":"n","role":"r","lat":40.65,"lon":-75.44}],
  "aviation":{"tafSample":{"raw":"TAF KABE 161720Z 1618/1724 27008KT P6SM","label":"sample","monthDays":31},
              "examples":[{"label":"l","raw":"KABE 181451Z 00000KT 10SM CLR 09/M01 A3062"}],
              "meetsCode":[{"habit":"h","inFlight":"a","inCode":"b"}]},
  "weather":{"KABE":{"raw":"KABE 1","observedAt":"2026-10-16T17:51:00Z","sample":true}},
  "highlights":[{"id":"h","value":"220+","label":"flight hours","source":"certifications"}],
  "finance":{"headline":["A.","B.","C."],"salutation":"Dear reader,","letter":"l","quote":"Long-only."},
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

TEST(site_check_validates_finance_copy_and_highlights) {
  Json site = load(kValidSite);
  // The pull quote must be one of the risk controls, never a separate claim.
  Json fin = site.at("finance");
  fin.set("quote", "Something else.");
  site.set("finance", fin);
  auto issues = fc::checkSite(site, {});
  CHECK_EQ(count(issues, Issue::Severity::Error), 1);
  CHECK_EQ(issues[0].path, std::string("finance.quote"));

  // Three words in the headline, and every highlight names its value, label and source.
  site = load(kValidSite);
  fin = site.at("finance");
  fin.set("headline", load(R"(["A.","B."])"));
  site.set("finance", fin);
  site.set("highlights", load(R"([{"id":"h","value":"5+","label":"hours"}])"));
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

namespace {

// The valid site with `fn` applied to one top-level object (for example "bot" or "finance").
Json edit(const char* key, const std::function<void(Json&)>& fn) {
  Json site = load(kValidSite);
  Json part = site.at(key);
  fn(part);
  site.set(key, part);
  return site;
}

int errors(const Json& site) { return count(fc::checkSite(site, {}), Issue::Severity::Error); }
int warnings(const Json& site) { return count(fc::checkSite(site, {}), Issue::Severity::Warning); }

}  // namespace

TEST(site_check_placeholders_are_found_in_any_case_after_spaces_or_a_bracket) {
  for (const char* text : {"TODO: add email", "todo add email", "Todo", "[TODO: add email]", "  [ todo ]", "TBD", "tbd", " [TBD] "}) {
    Json site = load(kValidSite);
    site.set("email", text);
    CHECK_EQ(warnings(site), 1);
    CHECK_EQ(errors(site), 0);
  }
  for (const char* text : {"a@b.c", "A TODO later", "TBD later", "tbdx", "[]", "  ", "Mr. Todd"}) {
    Json site = load(kValidSite);
    site.set("email", text);
    CHECK_EQ(warnings(site), 0);
  }
}

TEST(site_check_blocks_more_bot_keys_but_not_the_example_decision) {
  for (const char* key : {"position", "pnl", "amount", "holding", "PnL", "Position"}) {
    Json site = edit("bot", [&](Json& bot) { bot.set(key, 1); });
    CHECK_EQ(errors(site), 1);
  }
  for (const char* key : {"positionSize", "pnl_pct", "amounts"}) {
    CHECK_EQ(errors(edit("bot", [&](Json& bot) { bot.set(key, 1); })), 0);
  }
  // Nested keys are found too.
  CHECK_EQ(errors(edit("bot", [](Json& bot) { bot.set("benchmarks", load(R"([{"amount":1}])")); })), 1);
  // The example reply keeps its own keys.
  CHECK_EQ(errors(edit("bot", [](Json& bot) {
             bot.set("exampleDecision", load(R"({"action":"HOLD","suggested_tickers":[],"position":"none","amount":"n/a"})"));
           })), 0);
}

TEST(site_check_rejects_dollar_amounts_without_echoing_them) {
  for (const char* text : {"Risks up to $500 a trade.", "Risks $ 5 a trade.", "costs $0", "$1"}) {
    Json site = edit("bot", [&](Json& bot) { bot.set("riskControls", load((std::string(R"(["Long-only.",")") + text + "\"]").c_str())); });
    auto issues = fc::checkSite(site, {});
    CHECK_EQ(count(issues, Issue::Severity::Error), 1);
    CHECK_EQ(issues[0].path, std::string("bot.riskControls[1]"));
    CHECK(issues[0].message.find('$') == std::string::npos);
    CHECK(issues[0].message.find("500") == std::string::npos);
  }
  // A dollar sign that is not an amount is fine.
  CHECK_EQ(errors(edit("bot", [](Json& bot) { bot.set("riskControls", load(R"(["Long-only.","Pays in US$ only, or $ex"])")); })), 0);

  CHECK_EQ(errors(edit("finance", [](Json& f) { f.set("letter", "It made $12 last week."); })), 1);
  CHECK_EQ(errors(edit("finance", [](Json& f) { f.set("headline", load(R"(["A.","B.","C is $3"])")); })), 1);
  CHECK_EQ(errors(edit("bot", [](Json& bot) { bot.set("decisionFlow", load(R"([{"step":"Size","text":"Buys $100 at a time."}])")); })), 1);

  Json site = load(kValidSite);
  site.set("highlights", load(R"([{"id":"h","value":"$5k","label":"saved","source":"x"},{"id":"i","value":"2","label":"US $ 3 units","source":"x"}])"));
  CHECK_EQ(errors(site), 2);
  // The source line is a reference, not a claim shown beside the figure.
  site.set("highlights", load(R"([{"id":"h","value":"5k","label":"saved","source":"projects: $5 tool"}])"));
  CHECK_EQ(errors(site), 0);
  // The example reply is only an illustration.
  CHECK_EQ(errors(edit("bot", [](Json& bot) { bot.set("exampleDecision", load(R"({"action":"BUY","thesis":"Example $100"})")); })), 0);
}

TEST(site_check_project_and_experience_ids) {
  auto projects = [](const char* text) {
    Json site = load(kValidSite);
    site.set("projects", load(text));
    return site;
  };
  auto project = [](const char* id) {
    return std::string(R"({"id":")") + id + R"(","name":"n","tag":"t","summary":"s","stack":[],"status":"live","statusLabel":"x"})";
  };
  for (const char* id : {"a", "0a", "my-project", "a--b", "a-"}) CHECK_EQ(errors(projects(("[" + project(id) + "]").c_str())), 0);
  for (const char* id : {"-a", "A", "my_project", "a b", "é", "a.b"}) CHECK_EQ(errors(projects(("[" + project(id) + "]").c_str())), 1);
  CHECK_EQ(errors(projects(("[" + project("same") + "," + project("same") + "]").c_str())), 1);
  CHECK_EQ(errors(projects(("[" + project("one") + "," + project("two") + "]").c_str())), 0);

  Json site = load(kValidSite);
  site.set("experience", load(R"([{"role":"r","org":"o","start":"2026","description":"d"}])"));
  auto issues = fc::checkSite(site, {});
  CHECK_EQ(count(issues, Issue::Severity::Error), 2);  // id and end
  site.set("experience", load(R"([{"id":3,"role":"r","org":"o","start":"2026","end":4,"description":"d"}])"));
  CHECK_EQ(errors(site), 2);
}

TEST(site_check_links_must_be_https_and_the_resume_a_site_path) {
  for (const char* bad : {"http://github.com/x", "javascript:alert(1)", "//github.com/x", "github.com/x"}) {
    Json github = load(kValidSite);
    github.set("links", load((std::string(R"({"github":")") + bad + R"(","linkedin":"https://l"})").c_str()));
    CHECK_EQ(errors(github), 1);
    Json linkedin = load(kValidSite);
    linkedin.set("links", load((std::string(R"({"github":"https://g","linkedin":")") + bad + R"("})").c_str()));
    CHECK_EQ(errors(linkedin), 1);
    CHECK_EQ(errors(edit("bot", [&](Json& bot) { bot.set("apiBase", bad); })), 1);
  }
  for (const char* bad : {"http://example.com/r.pdf", "//evil.example/r.pdf", "r.pdf", "https://example.com/r.pdf", "javascript:alert(1)"}) {
    Json site = load(kValidSite);
    site.set("resume", bad);
    CHECK_EQ(errors(site), 1);
  }
  for (const char* ok : {"/r.pdf", "/files/Franco Caccia.pdf"}) {
    Json site = load(kValidSite);
    site.set("resume", ok);
    CHECK_EQ(errors(site), 0);
  }
  // A placeholder stays a warning, as before.
  for (const char* placeholder : {"TODO: add resume", "[todo]", "TBD"}) {
    Json site = load(kValidSite);
    site.set("resume", placeholder);
    CHECK_EQ(errors(site), 0);
    CHECK_EQ(warnings(site), 1);
  }
}

TEST(site_check_project_repo_and_stack_types) {
  auto with = [](const char* extra) {
    Json site = load(kValidSite);
    site.set("projects", load((std::string(R"([{"id":"p1","name":"n","tag":"t","summary":"s","status":"live","statusLabel":"x",)") + extra + "}]").c_str()));
    return site;
  };
  CHECK_EQ(errors(with(R"("stack":["Go"],"repo":"https://github.com/x/y")")), 0);
  CHECK_EQ(errors(with(R"("stack":["Go"],"repo":"")")), 0);
  CHECK_EQ(errors(with(R"("stack":["Go"],"repo":null)")), 0);  // no repository yet
  CHECK_EQ(errors(with(R"("stack":["Go"])")), 0);
  CHECK_EQ(errors(with(R"("stack":["Go"],"repo":"http://github.com/x/y")")), 1);
  CHECK_EQ(errors(with(R"("stack":["Go"],"repo":"git@github.com:x/y.git")")), 1);
  CHECK_EQ(errors(with(R"("stack":["Go"],"repo":5)")), 1);
  CHECK_EQ(errors(with(R"("stack":["Go"],"repo":["https://x"])")), 1);
  CHECK_EQ(errors(with(R"("stack":["Go",3,null])")), 2);
  CHECK_EQ(errors(with(R"("stack":"Go")")), 1);
}
