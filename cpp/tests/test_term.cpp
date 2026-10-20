#include <algorithm>
#include <cctype>

#include "fc/api.hpp"
#include "fc/json.hpp"
#include "fc/term.hpp"
#include "test.hpp"

using fc::Json;

namespace {

const char* kSite = R"({
  "name":"Franco Caccia","location":"Pennsylvania, USA",
  "positioning":"Quant and software developer.","openTo":"Open to quant roles.",
  "links":{"github":"https://github.com/Francocaccia","linkedin":"https://www.linkedin.com/in/x/"},
  "email":"TODO: add email","resume":"TODO: add resume","photo":"TODO","footer":"f",
  "projects":[
    {"id":"autotradingbot","name":"AutoTradingBot","tag":"ATB","status":"live","statusLabel":"Live, paper trading",
     "period":"January 2026 to present","summary":"Long-only portfolio manager in Go.","stack":["Go","Python"],"repo":"https://github.com/Francocaccia/AutoTradingBot"},
    {"id":"defi-lending","name":"DeFi lending protocol","tag":"DEFI","status":"shipped","statusLabel":"Shipped",
     "summary":"Lending with Chainlink.","stack":["Solidity"],"repo":"https://github.com/Francocaccia/defi-lending-core"},
    {"id":"mev-and-chain","name":"MEV detector","tag":"MEV","status":"planned","statusLabel":"Planned",
     "summary":"A detector.","stack":["Go"],"repo":null}],
  "experience":[
    {"id":"exp-lehigh","role":"Research Assistant","org":"Lehigh University","place":"Bethlehem, Pennsylvania","start":"2026","end":"present",
     "description":"Python pipelines."},
    {"id":"exp-fund","role":"Quant Analyst","org":"A fund","start":"2025","end":"2026","description":"Market analysis."}],
  "education":[{"degree":"M.S. Aerospace","school":"Lehigh University","place":"Bethlehem, Pennsylvania","period":"2026 to present"}],
  "coursework":["Finance","Machine Learning"],
  "technicalSkills":["Assembly","Go","Flex & Bison"],
  "certifications":[{"name":"Commercial Pilot","issuer":"ANAC","status":"active","detail":"220+ flight hours"},
                    {"name":"Flight Dispatcher","status":"in-progress"}],
  "languages":[{"name":"Spanish","level":"native"},{"name":"English","level":"bilingual"}],
  "skills":[{"name":"Go","group":"Languages","state":"used","usedIn":["autotradingbot"]},
            {"name":"Python","group":"Languages","state":"used","usedIn":["autotradingbot"]},
            {"name":"Docker","group":"Infrastructure","state":"used","usedIn":["autotradingbot"]}]
})";

Json parse(const std::string& text) {
  Json out;
  std::string error;
  bool ok = Json::parse(text, out, error);
  CHECK(ok);
  return out;
}

fc::Term makeTerm(bool withAviation = true) {
  fc::TermOptions o;
  o.modes = withAviation ? std::vector<std::string>{"aviation", "finance"} : std::vector<std::string>{"finance"};
  return fc::Term(parse(kSite), o);
}

struct Run {
  Json result;
  std::string text() const {
    std::string out;
    for (const Json& line : result.at("lines").items()) {
      for (const Json& span : line.items()) out += span.at("t").asString();
      out += "\n";
    }
    return out;
  }
  std::string cwd() const { return result.at("cwd").asString(); }
  const Json* action(const char* type) const {
    for (const Json& a : result.at("actions").items()) {
      if (a.at("type").asString() == type) return &a;
    }
    return nullptr;
  }
  // First span with this kind, or null.
  const Json* span(const char* kind) const {
    for (const Json& line : result.at("lines").items()) {
      for (const Json& s : line.items()) {
        if (s.at("k").asString() == kind) return &s;
      }
    }
    return nullptr;
  }
};

Run run(const fc::Term& t, const std::string& line, const std::string& cwd = "~") {
  return Run{t.exec(line, cwd, {line}, 0)};
}

bool has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

std::string lowerCopy(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

}  // namespace

TEST(term_builds_the_filesystem_from_the_data) {
  fc::Term t = makeTerm();
  Json files = t.files();
  std::vector<std::string> paths;
  for (const Json& f : files.items()) paths.push_back(f.at("path").asString());
  auto has_path = [&](const char* p) { return std::find(paths.begin(), paths.end(), p) != paths.end(); };
  CHECK(has_path("~/about.md"));
  CHECK(has_path("~/skills.txt"));
  CHECK(has_path("~/licenses.md"));
  CHECK(has_path("~/README.md"));
  CHECK(has_path("~/bot.json"));
  CHECK(has_path("~/contact.txt"));
  CHECK(has_path("~/projects/README.md"));
  CHECK(has_path("~/projects/autotradingbot.md"));
  CHECK(has_path("~/projects/mev-and-chain.md"));
  CHECK(has_path("~/experience/lehigh.md"));
  CHECK(has_path("~/experience/fund.md"));
  CHECK(has_path("~/experience/education.md"));
}

TEST(term_ls_lists_sorted_entries_with_markers) {
  fc::Term t = makeTerm();
  std::string out = run(t, "ls").text();
  CHECK(has(out, "about.md"));
  CHECK(has(out, "experience/"));
  CHECK(has(out, "projects/"));
  CHECK(has(out, "resume.pdf@"));
  CHECK(has(out, "README.md"));
  CHECK(has(out, "bot.json"));
  CHECK(out.find("about.md") < out.find("projects/"));
  std::string inner = run(t, "ls projects").text();
  CHECK(has(inner, "README.md"));
  CHECK(has(inner, "defi-lending.md"));
}

TEST(term_ls_click_commands_use_full_paths) {
  fc::Term t = makeTerm();
  Run r = run(t, "ls projects");
  bool found = false;
  for (const Json& line : r.result.at("lines").items()) {
    for (const Json& s : line.items()) {
      if (s.at("c").asString() == "cat ~/projects/README.md") found = true;
    }
  }
  CHECK(found);
}

TEST(term_cd_pwd_and_navigation) {
  fc::Term t = makeTerm();
  Run r = run(t, "cd projects");
  CHECK_EQ(r.cwd(), std::string("~/projects"));
  CHECK_EQ(run(t, "pwd", "~/projects").text(), std::string("/home/franco/projects\n"));
  CHECK_EQ(run(t, "cd ..", "~/projects").cwd(), std::string("~"));
  CHECK_EQ(run(t, "cd", "~/experience").cwd(), std::string("~"));
  CHECK_EQ(run(t, "cd ~/experience", "~/projects").cwd(), std::string("~/experience"));
  CHECK_EQ(run(t, "cd ../..", "~/projects").cwd(), std::string("~"));  // cannot go above home
  CHECK_EQ(run(t, "ls", "~/projects").text().find("README.md") != std::string::npos, true);
}

TEST(term_cd_errors_are_friendly) {
  fc::Term t = makeTerm();
  Run missing = run(t, "cd nope");
  CHECK(has(missing.text(), "no such folder"));
  CHECK_EQ(missing.cwd(), std::string("~"));
  CHECK(has(run(t, "cd about.md").text(), "is a file"));
  CHECK(has(run(t, "cd /etc").text(), "outside this site"));
  CHECK(run(t, "cd nope").span("error") != nullptr);
}

TEST(term_cat_reads_files_and_explains_mistakes) {
  fc::Term t = makeTerm();
  std::string about = run(t, "cat about.md").text();
  CHECK(has(about, "Franco Caccia"));
  CHECK(has(about, "Pennsylvania, USA"));
  CHECK(has(about, "Spanish (native)"));
  CHECK(has(run(t, "cat projects/autotradingbot.md").text(), "Repo: https://github.com/Francocaccia/AutoTradingBot"));
  CHECK(has(run(t, "cat projects/mev-and-chain.md").text(), "Repo: no public repo"));
  CHECK(has(run(t, "cat missing.txt").text(), "no such file"));
  CHECK(has(run(t, "cat projects").text(), "is a folder"));
  CHECK(has(run(t, "cat").text(), "Usage: cat <file>"));
  CHECK(has(run(t, "cat about.md contact.txt").text(), "--- contact.txt"));
}

TEST(term_files_carry_the_resume_details) {
  fc::Term t = makeTerm();
  CHECK(has(run(t, "cat experience/lehigh.md").text(), "Lehigh University, Bethlehem, Pennsylvania, 2026 to present"));
  CHECK(has(run(t, "cat experience/education.md").text(), "Lehigh University, Bethlehem, Pennsylvania, 2026 to present"));
  CHECK(has(run(t, "cat experience/education.md").text(), "Relevant coursework: Finance, Machine Learning."));
  CHECK(has(run(t, "cat licenses.md").text(), "Commercial Pilot (ANAC), 220+ flight hours"));
  CHECK(has(run(t, "cat skills.txt").text(), "Everything on the resume:"));
  CHECK(has(run(t, "cat skills.txt").text(), "Assembly, Go, Flex & Bison"));
  CHECK(has(run(t, "cat projects/autotradingbot.md").text(), "Period: January 2026 to present"));
}

TEST(term_cat_turns_urls_into_links_and_marks_placeholders) {
  fc::Term t = makeTerm();
  Run r = run(t, "cat contact.txt");
  const Json* link = r.span("link");
  CHECK(link != nullptr);
  if (link) CHECK_EQ(link->at("h").asString(), std::string("https://github.com/Francocaccia"));
  CHECK(has(r.text(), "[TODO: email]"));
  CHECK(has(r.text(), "[TODO: resume]"));
}

TEST(term_every_link_uses_a_safe_scheme) {
  fc::Term t = makeTerm();
  for (const char* cmd : {"cat contact.txt", "open github", "open linkedin", "open autotradingbot", "sudo hire franco",
                          "cat projects/defi-lending.md"}) {
    Run r = run(t, cmd);
    for (const Json& line : r.result.at("lines").items()) {
      for (const Json& s : line.items()) {
        if (s.at("k").asString() != "link") continue;
        const std::string& h = s.at("h").asString();
        CHECK(h.rfind("https://", 0) == 0 || h.rfind("mailto:", 0) == 0 || (h.size() > 1 && h[0] == '/'));
      }
    }
  }
}

TEST(term_names_can_be_typed_by_their_first_letters) {
  fc::Term t = makeTerm();
  Run r = run(t, "cat auto");  // the one file that starts with "auto"
  CHECK(has(r.text(), "(opening ~/projects/autotradingbot.md)"));
  CHECK(has(r.text(), "Long-only portfolio manager in Go."));
  CHECK(has(run(t, "cat proj/read").text(), "(opening ~/projects/README.md)"));
  CHECK_EQ(run(t, "cd exp").cwd(), std::string("~/experience"));
  CHECK(has(run(t, "ls pro").text(), "defi-lending.md"));
  // Several matches: it asks which one instead of guessing.
  Run both = run(t, "cat r");  // README.md and resume.pdf
  CHECK(has(both.text(), "matches more than one name"));
  CHECK(has(both.text(), "~/README.md"));
  CHECK(has(both.text(), "~/resume.pdf"));
  CHECK(has(run(t, "cat zzz").text(), "no such file"));
}

TEST(term_cd_slash_goes_to_the_top) {
  fc::Term t = makeTerm();
  CHECK_EQ(run(t, "cd /", "~/projects").cwd(), std::string("~"));
}

TEST(term_new_about_commands) {
  fc::Term t = makeTerm();
  std::string exp = run(t, "experience").text();
  CHECK(has(exp, "Research Assistant"));
  CHECK(has(exp, "Lehigh University, Bethlehem, Pennsylvania"));
  CHECK(has(exp, "Market analysis."));
  CHECK(has(run(t, "skills").text(), "Everything on the resume:"));
  Run lic = run(t, "licenses");
  CHECK(has(lic.text(), "[active]"));
  CHECK(has(lic.text(), "[in progress]"));
  CHECK(has(lic.text(), "220+ flight hours"));
  CHECK(lic.span("pos") != nullptr);
  Run contact = run(t, "contact");
  CHECK(has(contact.text(), "https://github.com/Francocaccia"));
  CHECK(has(contact.text(), "[TODO: email]"));
  CHECK(contact.span("link") != nullptr);
}

TEST(term_readme_and_bot_json_describe_the_whole_portfolio) {
  fc::Term t = makeTerm();
  std::string readme = run(t, "cat README.md").text();
  CHECK(has(readme, "Franco Caccia"));
  CHECK(has(readme, "Projects"));
  CHECK(has(readme, "AutoTradingBot [ATB]"));
  CHECK(has(readme, "Research Assistant, Lehigh University"));
  CHECK(has(readme, "Spanish (native)"));
  // The bot file carries rules only: no performance, positions or amounts.
  std::string bot = run(t, "cat bot.json").text();
  CHECK(has(bot, "paper trading only"));
  CHECK(has(bot, "tracking_since"));
  CHECK(!has(bot, "return"));
  CHECK(!has(bot, "$"));
}

TEST(term_tree_draws_every_file) {
  fc::Term t = makeTerm();
  std::string out = run(t, "tree").text();
  CHECK(out.rfind("~\n", 0) == 0);
  CHECK(has(out, "|-- about.md"));
  CHECK(has(out, "|-- projects/"));
  CHECK(has(out, "|   `-- README.md"));
  CHECK(has(out, "`-- skills.txt"));
  CHECK(has(run(t, "tree projects").text(), "~/projects"));
}

TEST(term_grep_is_case_sensitive_unless_asked) {
  fc::Term t = makeTerm();
  Run r = run(t, "grep -i PYTHON");
  CHECK(has(r.text(), "~/projects/autotradingbot.md"));
  CHECK(has(r.text(), "~/experience/lehigh.md"));
  const Json* match = r.span("match");
  CHECK(match != nullptr);
  if (match) CHECK_EQ(lowerCopy(match->at("t").asString()), std::string("python"));
  CHECK(has(r.text(), "matches in"));

  // Without -i the capitals must match, and the reply points at -i.
  Run strict = run(t, "grep python");
  CHECK(has(strict.text(), "No matches for 'python'"));
  CHECK(has(strict.text(), "Try: grep -i python"));
  CHECK(has(run(t, "grep Python").text(), "matches in"));

  CHECK(has(run(t, "grep zzzzzz").text(), "No matches"));
  CHECK(has(run(t, "grep").text(), "Usage: grep [-i] <text> [file]"));
}

TEST(term_grep_can_be_limited_to_a_file_or_folder) {
  fc::Term t = makeTerm();
  std::string one = run(t, "grep -i python experience/lehigh.md").text();
  CHECK(has(one, "~/experience/lehigh.md"));
  CHECK(!has(one, "~/projects/"));
  std::string folder = run(t, "grep -i go projects").text();
  CHECK(has(folder, "~/projects/"));
  CHECK(!has(folder, "~/experience/"));
}

TEST(term_user_input_is_never_markup) {
  fc::Term t = makeTerm();
  Run r = run(t, "echo \"<img src=x onerror=alert(1)>\"");
  CHECK_EQ(r.text(), std::string("<img src=x onerror=alert(1)>\n"));
  // The engine emits spans only: there is no field that carries HTML.
  CHECK(r.span("link") == nullptr);
  CHECK(has(run(t, "grep <script>").text(), "No matches for '<script>'"));
  CHECK(has(run(t, "<b>hi</b>").text(), "command not found: <b>hi</b>"));
}

TEST(term_unknown_command_suggests_help) {
  fc::Term t = makeTerm();
  Run r = run(t, "foobar");
  CHECK(has(r.text(), "command not found: foobar"));
  CHECK(has(r.text(), "help"));
  Run typo = run(t, "lss");
  CHECK(has(typo.text(), "Did you mean ls?"));
  CHECK(has(run(t, "cta").text(), "Did you mean cat?"));
}

TEST(term_help_lists_commands_and_explains_one) {
  fc::Term t = makeTerm();
  std::string all = run(t, "help").text();
  for (const char* c : {"ls", "cd", "pwd", "cat", "tree", "whoami", "neofetch", "projects", "stats", "grep", "history",
                        "clear", "echo", "date", "open", "mode", "metar", "exit", "help"}) {
    CHECK(has(all, c));
  }
  CHECK(has(all, "Tab finishes a name"));
  CHECK(has(run(t, "help ls").text(), "ls [folder]"));
  CHECK(has(run(t, "help nope").text(), "No help for 'nope'"));
}

TEST(term_whoami_neofetch_projects) {
  fc::Term t = makeTerm();
  CHECK(has(run(t, "whoami").text(), "Quant and software developer."));
  std::string card = run(t, "neofetch").text();
  CHECK(has(card, "franco@portfolio"));
  CHECK(has(card, "Pennsylvania, USA"));
  CHECK(has(card, "Go, Python"));
  CHECK(has(card, "Spanish (native)"));
  CHECK(has(card, "Open to   quant roles."));  // the label is not repeated in the value
  std::string list = run(t, "projects").text();
  CHECK(has(list, "ATB"));
  CHECK(has(list, "planned"));
  CHECK(has(list, "DeFi lending protocol"));
}

TEST(term_open_prints_links_for_known_targets) {
  fc::Term t = makeTerm();
  Run gh = run(t, "open github");
  CHECK(gh.span("link") != nullptr);
  CHECK(has(gh.text(), "https://github.com/Francocaccia"));
  CHECK(has(run(t, "open AutoTradingBot").text(), "github.com/Francocaccia/AutoTradingBot"));
  CHECK(has(run(t, "open defi-lending-core").text(), "defi-lending-core"));
  CHECK(has(run(t, "open mev-and-chain").text(), "no public repo"));
  CHECK(has(run(t, "open resume").text(), "not published yet"));
  CHECK(has(run(t, "open email").text(), "not published yet"));
  CHECK(has(run(t, "open nothing").text(), "I don't know 'nothing'"));
  CHECK(has(run(t, "open").text(), "What would you like to open"));
}

TEST(term_mode_navigates_only_to_enabled_views) {
  fc::Term t = makeTerm();
  Run r = run(t, "mode finance");
  const Json* nav = r.action("navigate");
  CHECK(nav != nullptr);
  if (nav) CHECK_EQ(nav->at("url").asString(), std::string("/finance/"));
  CHECK(r.action("remember") != nullptr);
  CHECK(run(t, "mode terminal").action("navigate") == nullptr);
  CHECK(has(run(t, "mode terminal").text(), "already"));
  CHECK(has(run(t, "mode bogus").text(), "unknown view"));

  fc::Term limited = makeTerm(false);
  CHECK(has(run(limited, "mode aviation").text(), "unknown view"));
  CHECK(run(limited, "mode aviation").action("navigate") == nullptr);
}

TEST(term_metar_decodes_the_snapshot_it_was_given) {
  fc::TermOptions o;
  o.modes = {"aviation"};
  o.metars["KABE"] = {"METAR KABE 181451Z 00000KT 10SM CLR 09/M01 A3062 RMK AO2 SLP371", "2026-10-18T14:51:00.000Z", false};
  o.metars["SABE"] = {"SABE 161800Z 18010KT 9999 FEW030 22/14 Q1015", "2026-10-16T18:00:00.000Z", true};
  fc::Term t(parse(kSite), o);
  std::string kabe = run(t, "metar KABE").text();
  CHECK(has(kabe, "METAR KABE 181451Z"));
  CHECK(has(kabe, "Observed 2026-10-18 14:51 UTC"));
  CHECK(!has(kabe, "sample data"));
  CHECK(has(kabe, "VFR"));
  CHECK(has(kabe, "Calm"));
  CHECK(has(kabe, "30.62 inHg"));
  CHECK(has(kabe, "mode aviation"));
  std::string sabe = run(t, "metar sabe").text();
  CHECK(has(sabe, "sample data, not a live report"));
  CHECK(has(sabe, "From 180"));
  CHECK(has(sabe, "1,015 hPa") || has(sabe, "1015 hPa"));
  // Only the airports it has reports for, and a sample format demo has no observation time.
  CHECK(has(run(t, "metar kjfk").text(), "only have reports for these airports"));
  o.metars["KBOS"] = {"KBOS 051156Z 05012G25KT 1 1/2SM -RA BR OVC008 09/08 A2981", "", true};
  fc::Term more(parse(kSite), o);
  std::string bos = run(more, "metar kbos").text();
  CHECK(has(bos, "Format demo"));
  CHECK(has(bos, "IFR"));
}

TEST(term_metar_completion_lists_the_airports_it_has) {
  fc::TermOptions o;
  o.metars["KABE"] = {"KABE 181451Z 00000KT 10SM CLR 09/M01 A3062", "2026-10-18T14:51:00.000Z", false};
  o.metars["SABE"] = {"SABE 161800Z 18010KT 9999 FEW030 22/14 Q1015", "2026-10-16T18:00:00.000Z", true};
  fc::Term t(parse(kSite), o);
  Json reply = t.complete("metar k", "~");
  CHECK_EQ(reply.at("options").items().size(), std::size_t(1));
  CHECK_EQ(reply.at("options").at(std::size_t(0)).asString(), std::string("kabe"));
}

TEST(term_metar_is_coming_soon_until_the_decoder_exists) {
  fc::Term t = makeTerm();
  CHECK(has(run(t, "metar kabe").text(), "coming soon"));
  CHECK(has(run(t, "metar sabe").text(), "mode aviation"));
  CHECK(has(run(t, "metar kjfk").text(), "coming soon"));
  CHECK(has(run(t, "metar").text(), "Usage: metar"));
}

TEST(term_easter_egg_and_sudo) {
  fc::Term t = makeTerm();
  std::string hire = run(t, "sudo hire franco").text();
  CHECK(has(hire, "Access granted"));
  CHECK(has(hire, "linkedin.com"));  // no email yet, so it points to LinkedIn
  CHECK(has(run(t, "sudo rm -rf /").text(), "not in the sudoers file"));
  CHECK(has(run(t, "sudo").text(), "usage: sudo"));
}

TEST(term_sudo_hire_prefers_email_when_present) {
  Json site = parse(kSite);
  site.set("email", "franco@example.com");
  fc::TermOptions o;
  fc::Term t(site, o);
  Run r = run(t, "sudo hire franco");
  const Json* link = r.span("link");
  CHECK(link != nullptr);
  if (link) CHECK_EQ(link->at("h").asString(), std::string("mailto:franco@example.com"));
}

TEST(term_actions_clear_exit_stats) {
  fc::Term t = makeTerm();
  CHECK(run(t, "clear").action("clear") != nullptr);
  Run leaving = run(t, "exit");
  const Json* nav = leaving.action("navigate");
  CHECK(nav != nullptr);
  if (nav) CHECK_EQ(nav->at("url").asString(), std::string("/"));
  CHECK(run(t, "stats").action("stats") != nullptr);
}

TEST(term_stats_placeholder_and_live_blocks_have_the_same_size) {
  fc::Term t = makeTerm();
  Run placeholder{t.statsPlaceholder("2026-10-13", true)};
  CHECK(has(placeholder.text(), "[+x.x%]"));
  CHECK(has(placeholder.text(), "Waking up the bot"));
  CHECK(has(placeholder.text(), "Illustrative data, not real results"));
  Json raw = parse(R"({"start_date":"2026-10-13","days_tracked":3,"bot_return_pct":-2.4,"excess_return_pct":-1.6,
    "bot_vs_60_40_pct":-2.24,"max_drawdown_pct":-2.48,"last_updated":"2026-10-16T22:23:56Z"})");
  Run live{t.statsOutput(raw, "2026-10-13", Json(), true)};
  CHECK(!has(live.text(), "[+x.x%]"));
  CHECK_EQ(live.result.at("lines").items().size(), placeholder.result.at("lines").items().size());
  Run failed{t.statsOutput(Json(), "2026-10-13", Json(), true)};
  CHECK(has(failed.text(), "did not answer"));
  CHECK(has(failed.text(), "[+x.x%]"));
  CHECK_EQ(failed.result.at("lines").items().size(), placeholder.result.at("lines").items().size());
}

TEST(term_stats_sparkline_uses_real_history_when_it_is_valid) {
  fc::Term t = makeTerm();
  Json history = parse(R"([{"date":"2026-10-13","c":100,"spy":100,"mix_60_40":100},
                           {"date":"2026-10-14","c":101,"spy":100.2,"mix_60_40":100.1},
                           {"date":"2026-10-15","c":99,"spy":100.4,"mix_60_40":100.2}])");
  Json raw = parse(R"({"start_date":"2026-10-13","days_tracked":3,"bot_return_pct":1})");
  Run r{t.statsOutput(raw, "2026-10-13", history)};
  CHECK(has(r.text(), "Bot close by day, since 2026-10-13, indexed to 100."));
  CHECK(!has(r.text(), "Illustrative"));
  // A history that fails validation falls back to the labeled illustration, never a mix.
  Run bad{t.statsOutput(raw, "2026-10-13", parse(R"([{"date":"x","c":1}])"))};
  CHECK(has(bad.text(), "Illustrative data, not real results"));
}

TEST(term_stats_output_uses_whitelisted_fields_only) {
  fc::Term t = makeTerm();
  Json raw = parse(R"({"start_date":"2026-10-13","days_tracked":3,"bot_return_pct":-2.4,"excess_return_pct":-1.6,
    "bot_vs_60_40_pct":-2.24,"max_drawdown_pct":-2.48,"open_positions_count":7,"num_trades":14,
    "allocation_pct":{"equities":28.24},"last_updated":"2026-10-16T22:23:56Z"})");
  Json reply = t.statsOutput(raw, "2026-10-13");
  Run r{reply};
  CHECK(has(r.text(), "since 2026-10-13"));
  CHECK(has(r.text(), "Return"));
  CHECK(has(r.text(), "\xE2\x88\x92" "2.40%"));
  CHECK(has(r.text(), "Only 3 trading days"));
  CHECK(has(r.text(), "Percentages only"));
  CHECK(!has(r.text(), "28.24"));
  CHECK(!has(r.text(), "14"));
  CHECK(r.span("neg") != nullptr);

  Run down{t.statsOutput(Json(), "2026-10-13")};
  CHECK(has(down.text(), "did not answer"));
}

TEST(term_history_echo_and_quotes) {
  fc::Term t = makeTerm();
  Json r = t.exec("history", "~", {"ls", "cd projects", "history"}, 0);
  std::string text = Run{r}.text();
  CHECK(has(text, "1   ls"));
  CHECK(has(text, "3   history"));
  CHECK_EQ(run(t, "echo \"a   b\" 'c d'").text(), std::string("a   b c d\n"));
  CHECK_EQ(run(t, "echo hello    world").text(), std::string("hello world\n"));
  CHECK(has(run(t, "echo \"oops").text(), "never closed"));
  CHECK_EQ(run(t, "   ").text(), std::string(""));
}

TEST(term_date_is_computed_from_the_unix_time_passed_in) {
  fc::Term t = makeTerm();
  auto at = [&](long long secs) { return Run{t.exec("date", "~", {}, secs)}.text(); };
  CHECK_EQ(at(0), std::string("Thu Jan  1 00:00:00 UTC 1970\n"));
  CHECK_EQ(at(86400LL * 365), std::string("Fri Jan  1 00:00:00 UTC 1971\n"));
  CHECK_EQ(at(951782400), std::string("Tue Feb 29 00:00:00 UTC 2000\n"));
  CHECK_EQ(at(951782400 + 13 * 3600 + 5 * 60 + 9), std::string("Tue Feb 29 13:05:09 UTC 2000\n"));
}

TEST(term_tab_completion) {
  fc::Term t = makeTerm();
  auto options = [&](const char* line, const char* cwd = "~") {
    std::vector<std::string> out;
    Json reply = t.complete(line, cwd);  // keep the reply alive while iterating its items
    for (const Json& o : reply.at("options").items()) out.push_back(o.asString());
    return out;
  };
  CHECK_EQ(options("he"), std::vector<std::string>{"help"});
  CHECK_EQ(options("cd pro"), std::vector<std::string>{"projects/"});
  CHECK_EQ(options("cat projects/au"), std::vector<std::string>{"projects/autotradingbot.md"});
  CHECK_EQ(options("cat au", "~/projects"), std::vector<std::string>{"autotradingbot.md"});
  CHECK_EQ(options("mode f"), std::vector<std::string>{"finance"});
  CHECK_EQ(options("sudo h"), std::vector<std::string>{"hire"});
  CHECK_EQ(options("open git"), std::vector<std::string>{"github"});
  CHECK_EQ(options("cd ~/"), (std::vector<std::string>{"~/experience/", "~/projects/"}));
  CHECK_EQ(options("cd ").size(), std::size_t(2));  // only folders
  CHECK(options("cat ").size() > 4);                // files and folders
  CHECK(options("zzz").empty());
  CHECK_EQ(t.complete("cat projects/au", "~").at("start").asNumber(), 4.0);
}

TEST(term_api_round_trip) {
  Json init = parse(fc::call("term.init", std::string(R"({"site":)") + kSite + R"(,"modes":["finance"],"files":true})"));
  CHECK(init.at("ok").asBool());
  CHECK(init.at("result").at("files").items().size() > 8);

  Json ran = parse(fc::call("term.exec", R"({"line":"cat about.md","cwd":"~","history":["cat about.md"],"now":0})"));
  CHECK(ran.at("ok").asBool());
  CHECK(ran.at("result").at("lines").items().size() > 3);

  Json comp = parse(fc::call("term.complete", R"({"line":"cd pro","cwd":"~"})"));
  CHECK_EQ(comp.at("result").at("options").at(std::size_t(0)).asString(), std::string("projects/"));

  Json stats = parse(fc::call("term.stats", R"({"stats":null,"trackingSince":"2026-10-13"})"));
  CHECK(stats.at("ok").asBool());

  Json bad = parse(fc::call("term.init", R"({"site":"nope"})"));
  CHECK(!bad.at("ok").asBool());
}

TEST(term_refuses_a_command_line_longer_than_1000_characters) {
  fc::Term t = makeTerm();
  Run edge = run(t, "echo " + std::string(995, 'a'));  // exactly 1000
  CHECK_EQ(edge.text(), std::string(995, 'a') + "\n");
  Run r = run(t, "echo " + std::string(996, 'a'));
  CHECK_EQ(r.text(), std::string("Command too long.\n"));
  CHECK(r.span("error") != nullptr);
  // The check comes before parsing, so an unclosed quote in a long line gets the same answer.
  CHECK_EQ(run(t, "echo \"" + std::string(2000, 'a')).text(), std::string("Command too long.\n"));
  std::string many;
  for (int i = 0; i < 5000; ++i) many += "cat README.md ";
  CHECK_EQ(run(t, many).text(), std::string("Command too long.\n"));
}

TEST(term_caps_one_commands_output_at_2000_lines) {
  fc::Term t = makeTerm();
  std::size_t one = run(t, "cat README.md").result.at("lines").items().size();
  CHECK(one > 10);
  // As many copies as fit on one line.
  std::string line = "cat";
  std::size_t copies = 0;
  while (line.size() + 10 <= 1000) line += " README.md", ++copies;
  Run r = run(t, line);
  const Json& lines = r.result.at("lines");
  CHECK(copies * one > 2000);
  CHECK_EQ(lines.items().size(), std::size_t(2001));
  const Json& last = lines.at(2000);
  CHECK_EQ(last.at(0).at("t").asString(), std::string("Output truncated at 2000 lines."));
  CHECK_EQ(last.at(0).at("k").asString(), std::string("dim"));
  CHECK(!has(run(t, "cat README.md").text(), "Output truncated"));
}

TEST(term_clickable_commands_quote_arguments_so_a_click_runs_the_same_command) {
  fc::Term t = makeTerm();
  Run miss = run(t, "grep \"python pipelines\"");
  CHECK(has(miss.text(), "No matches for 'python pipelines'"));
  std::string clicked;
  for (const Json& l : miss.result.at("lines").items()) {
    for (const Json& s : l.items()) {
      if (s.at("k").asString() == "cmd") clicked = s.at("c").asString();
    }
  }
  CHECK_EQ(clicked, std::string("grep -i \"python pipelines\""));
  Run again = run(t, clicked);
  CHECK(!has(again.text(), "No matches"));
  CHECK(has(again.text(), "matches in"));
  CHECK(has(again.text(), "~/experience/lehigh.md"));

  // A word without spaces stays bare, so the usual hint reads as before.
  CHECK(has(run(t, "grep python").text(), "Try: grep -i python"));
  // Quotes and backslashes in the text are escaped, not passed through.
  Run odd = run(t, "grep 'a\"b\\c d'");
  CHECK(has(odd.text(), "No matches"));
  for (const Json& l : odd.result.at("lines").items()) {
    for (const Json& s : l.items()) CHECK(s.at("k").asString() != "cmd");  // nothing found in another case, so no hint
  }
}

TEST(term_hrefs_with_backslashes_or_control_characters_never_become_links) {
  fc::Term t = makeTerm();
  auto githubLink = [](const char* href) {
    Json site = parse(kSite);
    Json links = site.at("links");
    links.set("github", href);
    site.set("links", links);
    fc::Term t(site, fc::TermOptions{});
    return run(t, "open github").span("link") != nullptr;
  };
  CHECK(githubLink("https://example.com/ok"));
  CHECK(!githubLink("https://example.com/a\\b"));
  CHECK(!githubLink("https://example.com/a\x01" "b"));
  CHECK(!githubLink("https://example.com/a\x7f" "b"));
  CHECK(!githubLink("mailto:x@y.z\\evil"));

  // The resume path comes from the data: only a plain site path is clickable.
  auto resumeLink = [](const char* resume) {
    Json site = parse(kSite);
    site.set("resume", resume);
    fc::TermOptions o;
    fc::Term t(site, o);
    return run(t, "open resume").span("link") != nullptr;
  };
  CHECK(resumeLink("/resume.pdf"));
  CHECK(!resumeLink("/\\evil.example/x"));
  CHECK(!resumeLink("//evil.example/x"));
  CHECK(!resumeLink("/a\\b"));
  CHECK(!resumeLink("/a\x01" "b"));
  CHECK(!resumeLink("/a\tb"));
}
