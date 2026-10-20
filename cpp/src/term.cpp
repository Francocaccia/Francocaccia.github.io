#include "fc/term.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <map>

#include "fc/chart.hpp"
#include "fc/stats.hpp"
#include "fc/weather.hpp"

namespace fc {

namespace {

constexpr const char* kUser = "visitor";
constexpr const char* kHome = "/home/franco";
constexpr std::size_t kCols = 44;  // layout width that still fits a 390px phone
constexpr std::size_t kMaxLineChars = 1000;  // longer command lines are refused before parsing
constexpr std::size_t kMaxOutputLines = 2000;  // one command never prints more than this

// ---------------------------------------------------------------- output model

struct Span {
  std::string text, kind, href, cmd;
};
using Line = std::vector<Span>;

Span S(std::string text, std::string kind = "") { return {std::move(text), std::move(kind), "", ""}; }

// Only these schemes may become links; anything else stays plain text.
bool safeHref(const std::string& h) {
  for (char ch : h) {
    unsigned char u = static_cast<unsigned char>(ch);
    if (ch == '\\' || u < 0x20 || u == 0x7f) return false;
  }
  if (h.rfind("https://", 0) == 0 || h.rfind("mailto:", 0) == 0) return true;
  return h.size() > 1 && h[0] == '/' && h[1] != '/';
}

Span LINK(std::string label, std::string href) {
  if (!safeHref(href)) return S(std::move(label));
  return {std::move(label), "link", std::move(href), ""};
}

// Quotes a word the way the shell's tokenizer reads it back: bare unless it holds a space, quote or
// backslash (or is empty), otherwise in double quotes with backslash and quote escaped.
std::string shellQuote(const std::string& s) {
  bool plain = !s.empty();
  for (char ch : s) {
    if (std::isspace(static_cast<unsigned char>(ch)) || ch == '"' || ch == '\'' || ch == '\\') plain = false;
  }
  if (plain) return s;
  std::string out = "\"";
  for (char ch : s) {
    if (ch == '"' || ch == '\\') out += '\\';
    out += ch;
  }
  return out + "\"";
}

// A clickable command: shows `label`, runs `command` (defaults to the label).
Span CMD(std::string label, std::string command = "") {
  std::string c = command.empty() ? label : std::move(command);
  return {std::move(label), "cmd", "", std::move(c)};
}

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::string pad(const std::string& s, std::size_t width) {
  return s.size() >= width ? s : s + std::string(width - s.size(), ' ');
}

std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> parts;
  std::string cur;
  for (char c : s) {
    if (c == sep) {
      parts.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  parts.push_back(cur);
  return parts;
}

std::string join(const std::vector<std::string>& v, const std::string& sep) {
  std::string out;
  for (std::size_t i = 0; i < v.size(); ++i) out += (i ? sep : "") + v[i];
  return out;
}

// Splits plain text at URLs so they become clickable.
Line linkify(const std::string& s, const std::string& kind = "") {
  Line out;
  std::string plain;
  std::size_t i = 0;
  while (i < s.size()) {
    if (s.compare(i, 8, "https://") == 0 || s.compare(i, 7, "mailto:") == 0) {
      std::size_t j = i;
      while (j < s.size() && !std::isspace(static_cast<unsigned char>(s[j]))) ++j;
      std::string url = s.substr(i, j - i);
      while (!url.empty() && std::strchr(".,);:", url.back())) url.pop_back();
      if (!plain.empty()) out.push_back(S(plain, kind)), plain.clear();
      out.push_back(LINK(url, url));
      i += url.size();
    } else {
      plain += s[i++];
    }
  }
  if (!plain.empty()) out.push_back(S(plain, kind));
  return out;
}

struct Out {
  std::vector<Line> lines;
  std::vector<Json> actions;

  bool truncated = false;

  // Lines past the cap are dropped; Term::exec closes the output with a notice.
  void push(Line l) {
    if (lines.size() >= kMaxOutputLines) {
      truncated = true;
      return;
    }
    lines.push_back(std::move(l));
  }
  void add(Line l) { push(std::move(l)); }
  void text(const std::string& s, const std::string& kind = "") { push({S(s, kind)}); }
  void blank() { push({}); }
  void error(const std::string& s) { text(s, "error"); }
  void action(const std::string& type, const std::string& key = "", const std::string& value = "") {
    Json a = Json::object();
    a.set("type", type);
    if (!key.empty()) a.set(key, value);
    actions.push_back(std::move(a));
  }
};

Json spanToJson(const Span& s) {
  Json o = Json::object();
  o.set("t", s.text);
  if (!s.kind.empty()) o.set("k", s.kind);
  if (!s.href.empty()) o.set("h", s.href);
  if (!s.cmd.empty()) o.set("c", s.cmd);
  return o;
}

Json linesToJson(const std::vector<Line>& lines) {
  Json arr = Json::array();
  for (const Line& l : lines) {
    Json row = Json::array();
    for (const Span& s : l) row.push(spanToJson(s));
    arr.push(std::move(row));
  }
  return arr;
}

// ------------------------------------------------------------------- filesystem

struct Node {
  enum class Kind { Dir, File, Link };
  Kind kind = Kind::File;
  std::string name;
  std::string content;  // files: the text. links: shown when read.
  std::string href;     // links
  std::vector<Node> children;
};

Node makeDir(const std::string& name) {
  Node n;
  n.kind = Node::Kind::Dir;
  n.name = name;
  return n;
}

Node makeFile(const std::string& name, const std::string& content) {
  Node n;
  n.kind = Node::Kind::File;
  n.name = name;
  n.content = content;
  return n;
}

void sortTree(Node& n) {
  std::sort(n.children.begin(), n.children.end(),
            [](const Node& a, const Node& b) { return lower(a.name) < lower(b.name); });
  for (Node& c : n.children) sortTree(c);
}

const Node* child(const Node& dir, const std::string& name) {
  for (const Node& c : dir.children) {
    if (c.name == name) return &c;
  }
  return nullptr;
}

const Node* nodeAt(const Node& root, const std::vector<std::string>& segs) {
  const Node* n = &root;
  for (const std::string& s : segs) {
    if (n->kind != Node::Kind::Dir) return nullptr;
    n = child(*n, s);
    if (!n) return nullptr;
  }
  return n;
}

using Path = std::vector<std::string>;

Path cwdSegments(const std::string& cwd) {
  Path out;
  if (cwd.empty() || cwd[0] != '~') return out;
  for (const std::string& p : split(cwd.substr(1), '/')) {
    if (!p.empty()) out.push_back(p);
  }
  return out;
}

std::string display(const Path& p) { return p.empty() ? "~" : "~/" + join(p, "/"); }

// Resolves `arg` against `cwd`. The filesystem root is the home folder; going
// above it is not possible and absolute paths outside it are rejected.
bool resolve(const Path& cwd, const std::string& arg, Path& out) {
  Path base = cwd;
  std::string rest = arg;
  if (arg.empty()) {
    out = cwd;
    return true;
  }
  if (arg == "/") {  // the root of this little filesystem is the home folder
    out.clear();
    return true;
  }
  if (arg == "~" || arg.rfind("~/", 0) == 0) {
    base.clear();
    rest = arg.substr(1);
  } else if (arg == kHome || arg.rfind(std::string(kHome) + "/", 0) == 0) {
    base.clear();
    rest = arg.substr(std::strlen(kHome));
  } else if (arg[0] == '/') {
    return false;
  }
  for (const std::string& part : split(rest, '/')) {
    if (part.empty() || part == ".") continue;
    if (part == "..") {
      if (!base.empty()) base.pop_back();
    } else {
      base.push_back(part);
    }
  }
  out = base;
  return true;
}

// ---------------------------------------------------------------------- content

std::string str(const Json& j, const char* key) { return j.at(key).asString(); }

std::string statusWord(const std::string& s) { return s == "next" ? "up next" : s; }

bool isTodo(const std::string& s) { return s.rfind("TODO", 0) == 0; }

// Placeholders in the data file show as clearly marked placeholders.
std::string orTodo(const std::string& value, const char* what) {
  return isTodo(value) ? std::string("[TODO: ") + what + "]" : value;
}

struct Site {
  const Json& j;
  std::string name() const { return str(j, "name"); }
  std::string email() const { return isTodo(str(j, "email")) ? "" : str(j, "email"); }
  std::string resume() const { return isTodo(str(j, "resume")) ? "" : str(j, "resume"); }
  std::string github() const { return str(j.at("links"), "github"); }
  std::string linkedin() const { return str(j.at("links"), "linkedin"); }

  std::string languagesLine() const {
    std::vector<std::string> v;
    for (const Json& l : j.at("languages").items()) v.push_back(str(l, "name") + " (" + str(l, "level") + ")");
    return join(v, ", ");
  }

  // Skills grouped by their `group`, in order of first appearance.
  std::vector<std::pair<std::string, std::vector<std::string>>> skillGroups() const {
    std::vector<std::pair<std::string, std::vector<std::string>>> groups;
    for (const Json& s : j.at("skills").items()) {
      std::string g = str(s, "group");
      auto it = std::find_if(groups.begin(), groups.end(), [&](const auto& p) { return p.first == g; });
      if (it == groups.end()) {
        groups.push_back({g, {}});
        it = groups.end() - 1;
      }
      it->second.push_back(str(s, "name") + (str(s, "state") == "upcoming" ? " (upcoming)" : ""));
    }
    return groups;
  }
};

std::string aboutText(const Site& s) {
  std::string t = "# " + s.name() + "\n" + str(s.j, "location") + "\n\n" + str(s.j, "positioning") + "\n" +
                  str(s.j, "openTo") + "\n\nSpoken languages: " + s.languagesLine() +
                  "\n\nNext: try projects, cat skills.txt, tree, or help.\n";
  return t;
}

std::string skillsText(const Site& s) {
  std::string t = "# Skills\nTools and languages, by group.\n\n";
  for (const auto& g : s.skillGroups()) t += pad(g.first, 16) + join(g.second, ", ") + "\n";
  std::vector<std::string> all;
  for (const Json& item : s.j.at("technicalSkills").items()) all.push_back(item.asString());
  if (!all.empty()) t += "\nEverything on the resume:\n" + join(all, ", ") + "\n";
  return t;
}

std::string licensesText(const Site& s) {
  std::string t = "# Licenses and certifications\n\n";
  for (const Json& c : s.j.at("certifications").items()) {
    std::string status = str(c, "status");
    if (status == "in-progress") status = "in progress";
    t += pad("[" + status + "]", 14) + str(c, "name");
    if (!str(c, "issuer").empty()) t += " (" + str(c, "issuer") + ")";
    if (!str(c, "detail").empty()) t += ", " + str(c, "detail");
    t += "\n";
  }
  return t;
}

std::string contactText(const Site& s) {
  std::string t = "# Contact\n\n";
  t += pad("GitHub:", 10) + s.github() + "\n";
  t += pad("LinkedIn:", 10) + s.linkedin() + "\n";
  t += pad("Email:", 10) + (s.email().empty() ? "[TODO: email]" : s.email()) + "\n";
  t += pad("Resume:", 10) + (s.resume().empty() ? "[TODO: resume]" : "type: open resume") + "\n";
  return t;
}

std::string projectText(const Json& p) {
  std::string t = "# " + str(p, "name") + "  [" + str(p, "tag") + "]\n";
  t += "Status: " + str(p, "statusLabel") + "\n";
  if (!str(p, "period").empty()) t += "Period: " + str(p, "period") + "\n";
  t += "\n" + str(p, "summary") + "\n\n";
  std::vector<std::string> stack;
  for (const Json& s : p.at("stack").items()) stack.push_back(s.asString());
  t += "Stack: " + join(stack, ", ") + "\n";
  t += p.at("repo").isString() ? "Repo: " + p.at("repo").asString() + "\n" : std::string("Repo: no public repo\n");
  return t;
}

std::string projectsReadme(const Site& s) {
  std::string t = "# Projects\n\n";
  for (const Json& p : s.j.at("projects").items()) {
    t += pad(str(p, "tag"), 6) + pad(statusWord(str(p, "status")), 9) + str(p, "name") + "\n";
  }
  t += "\nRead one with: cat projects/<name>.md\n";
  return t;
}

std::string roleText(const Json& e) {
  std::string where = str(e, "place").empty() ? "" : ", " + str(e, "place");
  return "# " + str(e, "role") + "\n" + str(e, "org") + where + ", " + str(e, "start") + " to " + str(e, "end") + "\n\n" +
         str(e, "description") + "\n";
}

std::string educationText(const Site& s) {
  std::string t = "# Education\n\n";
  for (const Json& e : s.j.at("education").items()) {
    std::string where = str(e, "place").empty() ? "" : ", " + str(e, "place");
    t += str(e, "degree") + "\n" + str(e, "school") + where + ", " + orTodo(str(e, "period"), "years") + "\n\n";
  }
  std::vector<std::string> courses;
  for (const Json& c : s.j.at("coursework").items()) courses.push_back(c.asString());
  if (!courses.empty()) t += "Relevant coursework: " + join(courses, ", ") + ".\n";
  return t;
}

std::string projectSlug(const Json& p) { return str(p, "id"); }

std::string experienceSlug(const Json& e) {
  std::string id = str(e, "id");
  return id.rfind("exp-", 0) == 0 ? id.substr(4) : id;
}

// The whole portfolio in one file, for people who would rather read than explore.
std::string readmeText(const Site& s) {
  std::string t = "# " + s.name() + "\n" + str(s.j, "location") + "\n\n" + str(s.j, "positioning") + "\n" + str(s.j, "openTo") + "\n\n";
  t += "GitHub: " + s.github() + "\nLinkedIn: " + s.linkedin() + "\n";
  t += "Email: " + (s.email().empty() ? std::string("[TODO: email]") : s.email()) + "\n\n";
  t += "## Projects\n";
  for (const Json& p : s.j.at("projects").items()) {
    t += "- " + str(p, "name") + " [" + str(p, "tag") + "], " + str(p, "statusLabel");
    if (!str(p, "period").empty()) t += ", " + str(p, "period");
    t += ". " + str(p, "summary") + "\n";
  }
  t += "\n## Experience\n";
  for (const Json& e : s.j.at("experience").items()) {
    t += "- " + str(e, "role") + ", " + str(e, "org") + " (" + str(e, "start") + " to " + str(e, "end") + "). " + str(e, "description") + "\n";
  }
  t += "\n## Education\n";
  for (const Json& e : s.j.at("education").items()) t += "- " + str(e, "degree") + ", " + str(e, "school") + " (" + orTodo(str(e, "period"), "years") + ")\n";
  t += "\n## Licenses and certifications\n";
  for (const Json& c : s.j.at("certifications").items()) {
    std::string status = str(c, "status");
    if (status == "in-progress") status = "in progress";
    t += "- " + str(c, "name") + (str(c, "issuer").empty() ? "" : " (" + str(c, "issuer") + ")") + ": " + status +
         (str(c, "detail").empty() ? "" : ", " + str(c, "detail")) + "\n";
  }
  t += "\n## Languages\n" + s.languagesLine() + "\n\n" + str(s.j, "footer") + "\n";
  return t;
}

// The bot's published settings. Percentages and rules only: no positions, no amounts.
std::string botJsonText(const Site& s) {
  const Json& bot = s.j.at("bot");
  Json o = Json::object();
  o.set("project", s.j.at("projects").at(std::size_t(0)).at("name").asString());
  o.set("mode", "paper trading only");
  o.set("tracking_since", bot.at("trackingSince").asString());
  o.set("benchmarks", bot.at("benchmarks"));
  Json bands = Json::array();
  for (const Json& b : bot.at("policyBands").items()) {
    Json band = Json::object();
    band.set("asset_class", b.at("label").asString()).set("min_pct", b.at("min")).set("max_pct", b.at("max"));
    bands.push(std::move(band));
  }
  o.set("policy_bands", std::move(bands));
  o.set("risk_controls", bot.at("riskControls"));
  o.set("history_feed_live", bot.at("historyLive").asBool(false));
  o.set("note", str(s.j, "footer"));
  return o.dump(2) + "\n";
}

Node buildTree(const Site& s) {
  Node root = makeDir("~");
  root.children.push_back(makeFile("README.md", readmeText(s)));
  root.children.push_back(makeFile("about.md", aboutText(s)));
  root.children.push_back(makeFile("skills.txt", skillsText(s)));
  root.children.push_back(makeFile("licenses.md", licensesText(s)));
  root.children.push_back(makeFile("contact.txt", contactText(s)));
  root.children.push_back(makeFile("bot.json", botJsonText(s)));

  Node resume;
  resume.kind = Node::Kind::Link;
  resume.name = "resume.pdf";
  resume.href = s.resume();
  resume.content = s.resume().empty() ? "[TODO: resume]" : "Resume";
  root.children.push_back(resume);

  Node projects = makeDir("projects");
  projects.children.push_back(makeFile("README.md", projectsReadme(s)));
  for (const Json& p : s.j.at("projects").items()) {
    projects.children.push_back(makeFile(projectSlug(p) + ".md", projectText(p)));
  }
  root.children.push_back(projects);

  Node experience = makeDir("experience");
  for (const Json& e : s.j.at("experience").items()) {
    experience.children.push_back(makeFile(experienceSlug(e) + ".md", roleText(e)));
  }
  experience.children.push_back(makeFile("education.md", educationText(s)));
  root.children.push_back(experience);

  sortTree(root);
  return root;
}

void collectFiles(const Node& n, const Path& at, std::vector<std::pair<std::string, const Node*>>& out) {
  for (const Node& c : n.children) {
    Path p = at;
    p.push_back(c.name);
    if (c.kind == Node::Kind::Dir) {
      collectFiles(c, p, out);
    } else if (c.kind == Node::Kind::File) {
      out.push_back({display(p), &c});
    }
  }
}

// Every file and folder with its path, for matching names the visitor only partly typed.
void collectAll(const Node& n, const Path& at, std::vector<std::pair<Path, const Node*>>& out) {
  for (const Node& c : n.children) {
    Path p = at;
    p.push_back(c.name);
    out.push_back({p, &c});
    if (c.kind == Node::Kind::Dir) collectAll(c, p, out);
  }
}

struct Loose {
  bool found = false;
  bool fuzzy = false;                  // true when a partly typed name was completed
  Path path;
  std::vector<std::string> ambiguous;  // display paths, when several names match
};

// Exact path first. Otherwise each part of the path may be a unique prefix ("cat auto" finds
// projects/autotradingbot.md), and a bare name may match a unique file or folder anywhere.
Loose resolveLoose(const Node& root, const Path& cwd, const std::string& arg) {
  Loose r;
  Path exact;
  if (resolve(cwd, arg, exact) && nodeAt(root, exact)) {
    r.found = true;
    r.path = exact;
    return r;
  }
  if (arg.empty() || arg[0] == '/' || arg[0] == '~') return r;

  Path wanted;
  if (!resolve(cwd, arg, wanted)) return r;
  const Node* n = &root;
  Path walked;
  bool ok = true;
  for (const std::string& seg : wanted) {
    const Node* next = n->kind == Node::Kind::Dir ? child(*n, seg) : nullptr;
    if (!next && n->kind == Node::Kind::Dir) {
      std::vector<const Node*> matches;
      for (const Node& c : n->children) {
        if (lower(c.name).rfind(lower(seg), 0) == 0) matches.push_back(&c);
      }
      if (matches.size() == 1) {
        next = matches[0];
      } else if (matches.size() > 1) {
        for (const Node* m : matches) {
          Path p = walked;
          p.push_back(m->name);
          r.ambiguous.push_back(display(p));
        }
        return r;
      }
    }
    if (!next) {
      ok = false;
      break;
    }
    walked.push_back(next->name);
    n = next;
  }
  if (ok) {
    r.found = true;
    r.fuzzy = true;
    r.path = walked;
    return r;
  }

  if (arg.find('/') == std::string::npos) {
    std::vector<std::pair<Path, const Node*>> all;
    collectAll(root, {}, all);
    std::vector<std::pair<Path, const Node*>> matches;
    for (const auto& item : all) {
      if (lower(item.second->name).rfind(lower(arg), 0) == 0) matches.push_back(item);
    }
    if (matches.size() == 1) {
      r.found = true;
      r.fuzzy = true;
      r.path = matches[0].first;
    } else {
      for (const auto& m : matches) r.ambiguous.push_back(display(m.first));
    }
  }
  return r;
}

// -------------------------------------------------------------------- commands

struct CommandInfo {
  const char* name;
  const char* group;
  const char* usage;
  const char* desc;
  const char* example;
};

const CommandInfo kCommands[] = {
    {"ls", "Look around", "ls [folder]", "List what is in a folder.", "ls projects"},
    {"cd", "Look around", "cd <folder>", "Go into a folder. cd .. goes back, cd alone goes home.", "cd projects"},
    {"pwd", "Look around", "pwd", "Show which folder you are in.", "pwd"},
    {"cat", "Look around", "cat <file>", "Read a file.", "cat about.md"},
    {"tree", "Look around", "tree [folder]", "Show every file at a glance.", "tree"},
    {"grep", "Look around", "grep [-i] <text> [file]", "Search every file (or one) for a word. -i ignores capitals.", "grep -i python"},
    {"whoami", "About me", "whoami", "One line about me.", "whoami"},
    {"neofetch", "About me", "neofetch", "A quick facts card.", "neofetch"},
    {"projects", "About me", "projects", "List my projects.", "projects"},
    {"experience", "About me", "experience", "Where I have worked.", "experience"},
    {"skills", "About me", "skills", "The tools and languages I use.", "skills"},
    {"licenses", "About me", "licenses", "Pilot licenses and certificates.", "licenses"},
    {"contact", "About me", "contact", "How to reach me.", "contact"},
    {"stats", "About me", "stats", "Status of the trading bot (percentages only).", "stats"},
    {"open", "Go somewhere", "open <github|linkedin|resume|project>", "Print a clickable link.", "open github"},
    {"mode", "Go somewhere", "mode <finance|aviation>", "Switch to another view of this site.", "mode finance"},
    {"metar", "Go somewhere", "metar <icao>", "Decode a weather report (try kabe or sabe).", "metar kabe"},
    {"exit", "Go somewhere", "exit", "Go back to the gateway.", "exit"},
    {"history", "Other", "history", "Show the commands you have run.", "history"},
    {"clear", "Other", "clear", "Clear the screen.", "clear"},
    {"echo", "Other", "echo <text>", "Print text back.", "echo hello"},
    {"date", "Other", "date", "Show the date and time (UTC).", "date"},
    {"help", "Other", "help [command]", "Show this list, or explain one command.", "help ls"},
};

struct Ctx {
  const Json& site;
  const TermOptions& options;
  const Node& root;
  Path cwd;
  std::vector<std::string> history;
  long long now;
  Out out;
  bool cwdChanged = false;

  Site s() const { return Site{site}; }
};

const CommandInfo* findCommand(const std::string& name) {
  for (const CommandInfo& c : kCommands) {
    if (name == c.name) return &c;
  }
  return nullptr;
}

std::size_t editDistance(const std::string& a, const std::string& b) {
  std::vector<std::size_t> prev(b.size() + 1), cur(b.size() + 1);
  for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = j;
  for (std::size_t i = 1; i <= a.size(); ++i) {
    cur[0] = i;
    for (std::size_t j = 1; j <= b.size(); ++j) {
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
    }
    std::swap(prev, cur);
  }
  return prev[b.size()];
}

void usage(Ctx& c, const std::string& command) {
  const CommandInfo* info = findCommand(command);
  Line l = {S("Usage: ", "dim"), S(info ? info->usage : command)};
  c.out.add(l);
  if (info) c.out.add({S("Example: ", "dim"), CMD(info->example)});
}

void cmdHelp(Ctx& c, const std::vector<std::string>& a) {
  if (!a.empty()) {
    const CommandInfo* info = findCommand(lower(a[0]));
    if (!info) {
      c.out.error("No help for '" + a[0] + "'.");
      c.out.add({S("Type "), CMD("help"), S(" for the full list.")});
      return;
    }
    c.out.add({S(info->usage, "accent")});
    c.out.text(std::string("  ") + info->desc);
    c.out.add({S("  Try: ", "dim"), CMD(info->example), S("  (click to run)", "dim")});
    return;
  }
  c.out.text("Welcome! This is a small shell. Type a command and press Enter, or just click any green command.", "");
  c.out.blank();
  const char* groups[] = {"Look around", "About me", "Go somewhere", "Other"};
  for (const char* g : groups) {
    c.out.text(std::string(g), "accent");
    for (const CommandInfo& info : kCommands) {
      if (std::strcmp(info.group, g) != 0) continue;
      c.out.add({S("  "), CMD(pad(info.name, 9), info.example), S(info.desc)});
    }
    c.out.blank();
  }
  c.out.add({S("Tips: Tab finishes a name. The up and down arrows bring back earlier commands.", "dim")});
}

void listDir(Ctx& c, const Node& dir, const Path& at) {
  std::size_t width = 0;
  for (const Node& n : dir.children) width = std::max(width, n.name.size() + (n.kind == Node::Kind::File ? 0 : 1));
  width += 2;
  std::size_t perLine = std::max<std::size_t>(1, kCols / width);
  Line row;
  std::size_t inRow = 0;
  for (const Node& n : dir.children) {
    std::string label = n.name + (n.kind == Node::Kind::Dir ? "/" : n.kind == Node::Kind::Link ? "@" : "");
    Path full = at;
    full.push_back(n.name);
    std::string command = (n.kind == Node::Kind::Dir ? "ls " : "cat ") + shellQuote(display(full));
    row.push_back(CMD(label, command));
    if (width > label.size()) row.push_back(S(std::string(width - label.size(), ' ')));
    if (++inRow == perLine) {
      c.out.add(row);
      row.clear();
      inRow = 0;
    }
  }
  if (!row.empty()) c.out.add(row);
}

// Explains a name that did not resolve: either several names match, or none do.
void reportMissing(Ctx& c, const std::string& command, const std::string& arg, const Loose& r, const std::string& noun) {
  if (!r.ambiguous.empty()) {
    c.out.error(command + ": '" + arg + "' matches more than one name. Which one?");
    for (const std::string& p : r.ambiguous) c.out.add({S("  "), CMD(p, command + " " + shellQuote(p))});
    return;
  }
  c.out.error(command + ": no such " + noun + " '" + arg + "'. Try: ls");
}

void noteResolved(Ctx& c, const Loose& r) {
  if (r.fuzzy) c.out.text("(opening " + display(r.path) + ")", "dim");
}

void cmdLs(Ctx& c, const std::vector<std::string>& a) {
  std::vector<std::string> targets;
  for (const std::string& x : a) {
    if (x.empty() || x[0] != '-') targets.push_back(x);  // flags like -l are accepted and ignored
  }
  if (targets.empty()) targets.push_back("");
  for (std::size_t i = 0; i < targets.size(); ++i) {
    Loose r = resolveLoose(c.root, c.cwd, targets[i]);
    if (!r.found) {
      reportMissing(c, "ls", targets[i], r, "file or folder");
      continue;
    }
    Path p = r.path;
    const Node* n = nodeAt(c.root, p);
    noteResolved(c, r);
    if (targets.size() > 1) c.out.text(targets[i] + ":", "dim");
    if (n->kind == Node::Kind::Dir) {
      listDir(c, *n, p);
    } else {
      c.out.add({CMD(n->name, "cat " + shellQuote(display(p)))});
    }
  }
  if (a.empty() || a.size() == targets.size()) {
    c.out.add({S("Click a name, or type: cat <file>   cd <folder>", "dim")});
  }
}

void cmdCd(Ctx& c, const std::vector<std::string>& a) {
  Path p;
  std::string arg = a.empty() ? "~" : a[0];
  if (!resolve(c.cwd, arg, p)) {
    c.out.error("cd: '" + arg + "' is outside this site. Try: cd ~");
    return;
  }
  Loose r = resolveLoose(c.root, c.cwd, arg);
  if (!r.found) {
    reportMissing(c, "cd", arg, r, "folder");
    return;
  }
  p = r.path;
  const Node* n = nodeAt(c.root, p);
  if (n->kind != Node::Kind::Dir) {
    c.out.error("cd: '" + arg + "' is a file, not a folder.");
    c.out.add({S("Read it with: ", "dim"), CMD("cat " + shellQuote(arg))});
    return;
  }
  c.cwd = p;
  c.cwdChanged = true;
}

void cmdPwd(Ctx& c, const std::vector<std::string>&) {
  c.out.text(std::string(kHome) + (c.cwd.empty() ? "" : "/" + join(c.cwd, "/")));
}

// Prints text the way `cat` does: "# " lines are headings, URLs become links.
void printContent(Ctx& c, const std::string& content) {
  std::string cur;
  for (std::size_t i = 0; i <= content.size(); ++i) {
    if (i == content.size() || content[i] == '\n') {
      if (i == content.size() && cur.empty()) break;
      if (cur.rfind("# ", 0) == 0) {
        c.out.text(cur.substr(2), "accent");
      } else if (cur.rfind("## ", 0) == 0) {
        c.out.text(cur.substr(3), "head");
      } else {
        c.out.add(linkify(cur));
      }
      cur.clear();
    } else {
      cur += content[i];
    }
  }
}

void printFile(Ctx& c, const Node& n) {
  if (n.kind == Node::Kind::Link) {
    if (n.href.empty()) {
      c.out.text(n.content, "dim");
    } else {
      c.out.add({S("Resume: ", "dim"), LINK(n.href, n.href)});
    }
    return;
  }
  printContent(c, n.content);
}

void cmdCat(Ctx& c, const std::vector<std::string>& a) {
  if (a.empty()) return usage(c, "cat");
  for (std::size_t i = 0; i < a.size(); ++i) {
    Loose r = resolveLoose(c.root, c.cwd, a[i]);
    if (!r.found) {
      reportMissing(c, "cat", a[i], r, "file");
      continue;
    }
    const Node* n = nodeAt(c.root, r.path);
    if (n->kind == Node::Kind::Dir) {
      c.out.error("cat: '" + a[i] + "' is a folder, not a file.");
      c.out.add({S("List it with: ", "dim"), CMD("ls " + shellQuote(display(r.path)))});
      continue;
    }
    if (a.size() > 1) c.out.text("--- " + a[i], "dim");
    noteResolved(c, r);
    printFile(c, *n);
  }
}

void drawTree(const Node& dir, const Path& at, const std::string& prefix, std::vector<Line>& lines) {
  for (std::size_t i = 0; i < dir.children.size(); ++i) {
    const Node& n = dir.children[i];
    bool last = i + 1 == dir.children.size();
    Path full = at;
    full.push_back(n.name);
    std::string label = n.name + (n.kind == Node::Kind::Dir ? "/" : n.kind == Node::Kind::Link ? "@" : "");
    lines.push_back({S(prefix + (last ? "`-- " : "|-- ")), CMD(label, (n.kind == Node::Kind::Dir ? "ls " : "cat ") + shellQuote(display(full)))});
    if (n.kind == Node::Kind::Dir) drawTree(n, full, prefix + (last ? "    " : "|   "), lines);
  }
}

void cmdTree(Ctx& c, const std::vector<std::string>& a) {
  std::string arg = a.empty() ? "" : a[0];
  Loose r = resolveLoose(c.root, c.cwd, arg);
  const Node* n = r.found ? nodeAt(c.root, r.path) : nullptr;
  if (!n || n->kind != Node::Kind::Dir) {
    c.out.error("tree: no such folder '" + arg + "'.");
    return;
  }
  c.out.text(display(r.path), "accent");
  std::vector<Line> lines;
  drawTree(*n, r.path, "", lines);
  for (Line& l : lines) c.out.add(std::move(l));
}

void cmdGrep(Ctx& c, const std::vector<std::string>& args) {
  // grep [-i] <text> [file]: -i ignores capitals; a last word that names a file or folder limits the search.
  bool ignoreCase = false;
  std::vector<std::string> a;
  for (const std::string& x : args) {
    if (x == "-i") ignoreCase = true;
    else a.push_back(x);
  }
  if (a.empty()) return usage(c, "grep");
  std::vector<std::pair<std::string, const Node*>> files;
  collectFiles(c.root, {}, files);
  if (a.size() >= 2) {
    Loose r = resolveLoose(c.root, c.cwd, a.back());
    if (r.found) {
      const Node* target = nodeAt(c.root, r.path);
      std::vector<std::pair<std::string, const Node*>> narrowed;
      std::string where = display(r.path);
      for (const auto& f : files) {
        bool inside = target->kind == Node::Kind::Dir ? f.first.rfind(where + "/", 0) == 0 : f.first == where;
        if (inside) narrowed.push_back(f);
      }
      files = narrowed;
      a.pop_back();
    }
  }
  std::string text = join(a, " ");
  std::string needle = ignoreCase ? lower(text) : text;
  constexpr std::size_t kMaxHits = 30;
  std::size_t hits = 0, filesHit = 0, otherCase = 0;
  for (const auto& f : files) {
    bool any = false;
    std::size_t lineNo = 0;
    for (const std::string& line : split(f.second->content, '\n')) {
      ++lineNo;
      std::size_t pos = (ignoreCase ? lower(line) : line).find(needle);
      if (pos == std::string::npos) {
        if (!ignoreCase && lower(line).find(lower(text)) != std::string::npos) ++otherCase;
        continue;
      }
      if (!any) ++filesHit;
      any = true;
      if (++hits > kMaxHits) continue;
      std::size_t from = pos > 24 ? pos - 24 : 0;
      std::size_t to = std::min(line.size(), pos + needle.size() + 32);
      Line row = {CMD(f.first, "cat " + shellQuote(f.first)), S(":" + std::to_string(lineNo) + ": ", "dim")};
      if (from > 0) row.push_back(S("...", "dim"));
      row.push_back(S(line.substr(from, pos - from)));
      row.push_back(S(line.substr(pos, needle.size()), "match"));
      row.push_back(S(line.substr(pos + needle.size(), to - pos - needle.size())));
      if (to < line.size()) row.push_back(S("...", "dim"));
      c.out.add(std::move(row));
    }
  }
  if (hits == 0) {
    c.out.text("No matches for '" + text + "'.", "dim");
    if (otherCase > 0) c.out.add({S("It does appear with other capitals. Try: ", "dim"), CMD("grep -i " + shellQuote(text))});
    else c.out.text("Try a shorter word, for example: grep -i go", "dim");
    return;
  }
  std::string summary = std::to_string(hits) + (hits == 1 ? " match" : " matches") + " in " + std::to_string(filesHit) +
                        (filesHit == 1 ? " file." : " files.");
  if (hits > kMaxHits) summary += " Showing the first " + std::to_string(kMaxHits) + ". Try a longer search.";
  c.out.text(summary, "dim");
}

void cmdWhoami(Ctx& c, const std::vector<std::string>&) {
  Site s = c.s();
  c.out.text(s.name(), "accent");
  c.out.text(str(s.j, "positioning"));
}

void cmdNeofetch(Ctx& c, const std::vector<std::string>&) {
  Site s = c.s();
  const char* logo[] = {"  ####   ####", "  #     #", "  ###   #", "  #     #", "  #      ####"};
  for (const char* l : logo) c.out.text(l, "accent");
  c.out.blank();
  c.out.add({S("franco", "accent"), S("@"), S("portfolio", "accent")});
  c.out.text("----------------");
  auto row = [&](const char* key, const std::string& value) {
    c.out.add({S(pad(key, 10), "accent"), S(value)});
  };
  row("Name", s.name());
  row("Location", str(s.j, "location"));
  row("Studying", str(s.j.at("education").at(std::size_t(0)), "degree"));
  if (!s.j.at("certifications").items().empty()) {
    const Json& cert = s.j.at("certifications").at(std::size_t(0));
    row("Pilot", str(cert, "name") + " (" + str(cert, "issuer") + ")");
  }
  for (const auto& g : s.skillGroups()) {
    if (g.first == "Languages") row("Code", join(g.second, ", "));
  }
  row("Spoken", s.languagesLine());
  row("Projects", std::to_string(s.j.at("projects").items().size()));
  std::string open = str(s.j, "openTo");
  if (open.rfind("Open to ", 0) == 0) open = open.substr(8);
  row("Open to", open);
}

void cmdProjects(Ctx& c, const std::vector<std::string>&) {
  Site s = c.s();
  c.out.add({S(pad("SYMBOL", 8) + pad("STATUS", 9) + "PROJECT", "dim")});
  for (const Json& p : s.j.at("projects").items()) {
    c.out.add({S(pad(str(p, "tag"), 8), "accent"), S(pad(statusWord(str(p, "status")), 9)),
               CMD(str(p, "name"), "cat projects/" + projectSlug(p) + ".md")});
  }
  c.out.blank();
  c.out.add({S("Click a name to read it, or type: ", "dim"), CMD("cat projects/autotradingbot.md")});
}

void cmdExperience(Ctx& c, const std::vector<std::string>&) {
  Site s = c.s();
  bool first = true;
  for (const Json& e : s.j.at("experience").items()) {
    if (!first) c.out.blank();
    first = false;
    std::string where = str(e, "place").empty() ? "" : ", " + str(e, "place");
    c.out.add({S(pad(str(e, "tag"), 6), "accent"), CMD(str(e, "role"), "cat ~/experience/" + experienceSlug(e) + ".md")});
    c.out.add({S("      "), S(str(e, "org") + where, "head")});
    c.out.add({S("      "), S(str(e, "start") + " to " + str(e, "end"), "dim")});
    c.out.add({S("      "), S(str(e, "description"))});
  }
}

void cmdSkills(Ctx& c, const std::vector<std::string>&) {
  Site s = c.s();
  printContent(c, skillsText(s));
}

void cmdLicenses(Ctx& c, const std::vector<std::string>&) {
  Site s = c.s();
  for (const Json& cert : s.j.at("certifications").items()) {
    std::string status = str(cert, "status");
    bool done = status == "active" || status == "complete";
    if (status == "in-progress") status = "in progress";
    std::string rest = str(cert, "name");
    if (!str(cert, "issuer").empty()) rest += " (" + str(cert, "issuer") + ")";
    c.out.add({S(pad("[" + status + "]", 14), done ? "pos" : "dim"), S(rest)});
    if (!str(cert, "detail").empty()) c.out.add({S("              "), S(str(cert, "detail"), "dim")});
  }
}

void cmdContact(Ctx& c, const std::vector<std::string>&) {
  Site s = c.s();
  c.out.add({S(pad("GitHub", 10), "dim"), LINK(s.github(), s.github())});
  c.out.add({S(pad("LinkedIn", 10), "dim"), LINK(s.linkedin(), s.linkedin())});
  if (s.email().empty()) c.out.add({S(pad("Email", 10), "dim"), S("[TODO: email]")});
  else c.out.add({S(pad("Email", 10), "dim"), LINK(s.email(), "mailto:" + s.email())});
  if (s.resume().empty()) c.out.add({S(pad("Resume", 10), "dim"), S("[TODO: resume]")});
  else c.out.add({S(pad("Resume", 10), "dim"), LINK(s.resume(), s.resume())});
}

void cmdStats(Ctx& c, const std::vector<std::string>&) {
  c.out.text("Asking the bot for its public stats. The free host can take about a minute to wake up...", "dim");
  c.out.action("stats");
}

struct Target {
  std::string key, label, href;
};

std::string lastSegment(const std::string& url) {
  std::string u = url;
  while (!u.empty() && u.back() == '/') u.pop_back();
  std::size_t i = u.find_last_of('/');
  return i == std::string::npos ? u : u.substr(i + 1);
}

std::string slug(const std::string& s) {
  std::string out;
  for (char ch : lower(s)) out += std::isalnum(static_cast<unsigned char>(ch)) ? ch : '-';
  return out;
}

void cmdOpen(Ctx& c, const std::vector<std::string>& a) {
  Site s = c.s();
  if (a.empty()) {
    c.out.text("What would you like to open?", "accent");
    c.out.add({S("  "), CMD("open github"), S("   "), CMD("open linkedin"), S("   "), CMD("open resume")});
    for (const Json& p : s.j.at("projects").items()) {
      if (p.at("repo").isString()) c.out.add({S("  "), CMD("open " + projectSlug(p))});
    }
    return;
  }
  std::string key = lower(a[0]);
  if (key == "github") return c.out.add({S("GitHub -> ", "dim"), LINK(s.github(), s.github())});
  if (key == "linkedin") return c.out.add({S("LinkedIn -> ", "dim"), LINK(s.linkedin(), s.linkedin())});
  if (key == "resume") {
    if (s.resume().empty()) return c.out.text("The resume is not published yet. [TODO: resume]", "dim");
    return c.out.add({S("Resume -> ", "dim"), LINK(s.resume(), s.resume())});
  }
  if (key == "email") {
    if (s.email().empty()) return c.out.text("The email address is not published yet. [TODO: email]", "dim");
    return c.out.add({S("Email -> ", "dim"), LINK(s.email(), "mailto:" + s.email())});
  }
  for (const Json& p : s.j.at("projects").items()) {
    std::string repo = p.at("repo").isString() ? lastSegment(p.at("repo").asString()) : "";
    bool match = key == lower(projectSlug(p)) || key == lower(str(p, "tag")) || key == slug(str(p, "name")) ||
                 (!repo.empty() && key == lower(repo));
    if (!match) continue;
    if (!p.at("repo").isString()) {
      c.out.text(str(p, "name") + " has no public repo.", "dim");
      c.out.add({S("Read about it: ", "dim"), CMD("cat projects/" + projectSlug(p) + ".md")});
      return;
    }
    return c.out.add({S(str(p, "name") + " -> ", "dim"), LINK(p.at("repo").asString(), p.at("repo").asString())});
  }
  c.out.error("open: I don't know '" + a[0] + "'.");
  c.out.add({S("Try: ", "dim"), CMD("open")});
}

void cmdMode(Ctx& c, const std::vector<std::string>& a) {
  auto enabled = [&](const std::string& m) {
    return std::find(c.options.modes.begin(), c.options.modes.end(), m) != c.options.modes.end();
  };
  if (a.empty()) {
    c.out.text("This site has more than one view of the same facts:", "accent");
    for (const std::string& m : c.options.modes) c.out.add({S("  "), CMD("mode " + m)});
    c.out.text("You are in the terminal view now.", "dim");
    return;
  }
  std::string m = lower(a[0]);
  if (m == "terminal") return c.out.text("You are already in the terminal view.", "dim");
  if (!enabled(m)) {
    c.out.error("mode: unknown view '" + a[0] + "'.");
    std::vector<std::string> options;
    for (const std::string& x : c.options.modes) options.push_back(x);
    c.out.text("Options: " + join(options, ", "), "dim");
    return;
  }
  std::string label = m;
  label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
  c.out.text("Switching to the " + label + " view...", "dim");
  c.out.action("navigate", "url", "/" + m + "/");
  c.out.action("remember", "mode", m);
}

std::string upperCopy(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
  return s;
}

// "2026-10-18T15:00:00.000Z" -> "2026-10-18 15:00 UTC"
std::string utcStamp(const std::string& iso) {
  if (iso.size() >= 16 && iso[10] == 'T') return iso.substr(0, 10) + " " + iso.substr(11, 5) + " UTC";
  return iso;
}

void cmdMetar(Ctx& c, const std::vector<std::string>& a) {
  if (a.empty()) return usage(c, "metar");
  std::string st = upperCopy(a[0]);
  bool aviation = std::find(c.options.modes.begin(), c.options.modes.end(), "aviation") != c.options.modes.end();
  if (c.options.metars.empty()) {
    c.out.text("METAR decoding is coming soon. It will live in the Aviation view.", "dim");
    if (aviation) c.out.add({S("Take a look: ", "dim"), CMD("mode aviation")});
    return;
  }
  auto found = c.options.metars.find(st);
  if (found == c.options.metars.end()) {
    c.out.error("metar: I only have reports for these airports:");
    Line row = {S("  ")};
    for (const auto& m : c.options.metars) {
      row.push_back(CMD(lower(m.first), "metar " + lower(m.first)));
      row.push_back(S("  "));
    }
    c.out.add(row);
    return;
  }
  Json d = decodeMetar(found->second.raw, utcStamp(found->second.observedAt));
  if (!d.at("ok").asBool()) {
    c.out.error("metar: " + d.at("error").asString());
    return;
  }
  c.out.add({S(found->second.raw, "accent")});
  c.out.add({S(found->second.observedAt.empty() ? "Format demo" : "Observed " + utcStamp(found->second.observedAt), "dim"),
             S(found->second.sample ? "  (sample data, not a live report)" : "", "warn")});
  c.out.blank();
  c.out.add({S(pad("Category", 13), "dim"), S(d.at("category").asString(), "accent"), S("  " + d.at("categoryWhy").asString())});
  static const char* const wanted[] = {"Wind (true direction)", "Visibility", "Weather", "Clouds", "Temperature",
                                       "Dew point",              "Pressure",   "Trend"};
  for (const char* label : wanted) {
    for (const Json& r : d.at("rows").items()) {
      if (r.at("label").asString() != label) continue;
      bool first = true;
      for (const std::string& line : split(r.at("value").asString(), '\n')) {
        std::string shown = first ? std::string(label) == "Wind (true direction)" ? "Wind" : label : "";
        c.out.add({S(pad(shown, 13), "dim"), S(line)});
        first = false;
      }
    }
  }
  c.out.blank();
  if (aviation) c.out.add({S("Every group explained: ", "dim"), CMD("mode aviation")});
}

void cmdSudo(Ctx& c, const std::vector<std::string>& a) {
  Site s = c.s();
  if (a.size() == 2 && lower(a[0]) == "hire" && lower(a[1]) == "franco") {
    c.out.text(std::string("[sudo] password for ") + kUser + ": ********", "dim");
    c.out.text("Access granted. Request received: hire franco.", "accent");
    c.out.text(str(s.j, "openTo"));
    if (!s.email().empty()) {
      c.out.add({S("Say hello: "), LINK(s.email(), "mailto:" + s.email())});
    } else {
      c.out.add({S("Say hello: "), LINK(s.linkedin(), s.linkedin())});
      c.out.text("(An email address is coming soon.)", "dim");
    }
    return;
  }
  if (a.empty()) return c.out.text("usage: sudo <command>", "dim");
  c.out.error(std::string(kUser) + " is not in the sudoers file. This incident will not be reported.");
}

void cmdHistory(Ctx& c, const std::vector<std::string>&) {
  std::size_t i = 1;
  for (const std::string& h : c.history) c.out.add({S(pad(std::to_string(i++), 4), "dim"), S(h)});
}

void cmdEcho(Ctx& c, const std::vector<std::string>& a) { c.out.text(join(a, " ")); }

void cmdClear(Ctx& c, const std::vector<std::string>&) { c.out.action("clear"); }

void cmdExit(Ctx& c, const std::vector<std::string>&) {
  c.out.text("Leaving the shell. Taking you back to the gateway...", "dim");
  c.out.action("navigate", "url", "/");
}

void civil(long long z, int& y, unsigned& m, unsigned& d) {
  z += 719468;
  long long era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned doe = static_cast<unsigned>(z - era * 146097);
  unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  y = static_cast<int>(yoe) + static_cast<int>(era) * 400;
  unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  if (m <= 2) ++y;
}

void cmdDate(Ctx& c, const std::vector<std::string>&) {
  static const char* const days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char* const months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  long long secs = c.now;
  long long day = secs >= 0 ? secs / 86400 : -((-secs + 86399) / 86400);
  long long rem = secs - day * 86400;
  int y;
  unsigned m, d;
  civil(day, y, m, d);
  int wd = static_cast<int>(((day % 7) + 7 + 4) % 7);
  char buf[64];
  std::snprintf(buf, sizeof buf, "%s %s %2u %02d:%02d:%02d UTC %d", days[wd], months[m - 1], d, static_cast<int>(rem / 3600),
                static_cast<int>(rem % 3600 / 60), static_cast<int>(rem % 60), y);
  c.out.text(buf);
}

using Handler = void (*)(Ctx&, const std::vector<std::string>&);

const std::map<std::string, Handler>& handlers() {
  static const std::map<std::string, Handler> table = {
      {"help", cmdHelp},     {"ls", cmdLs},         {"cd", cmdCd},         {"pwd", cmdPwd},   {"cat", cmdCat},
      {"tree", cmdTree},     {"grep", cmdGrep},     {"whoami", cmdWhoami}, {"neofetch", cmdNeofetch},
      {"projects", cmdProjects}, {"stats", cmdStats}, {"open", cmdOpen},   {"mode", cmdMode}, {"metar", cmdMetar},
      {"experience", cmdExperience}, {"skills", cmdSkills}, {"licenses", cmdLicenses}, {"contact", cmdContact},
      {"sudo", cmdSudo},     {"history", cmdHistory}, {"echo", cmdEcho},   {"clear", cmdClear}, {"date", cmdDate},
      {"exit", cmdExit},
  };
  return table;
}

// Splits a command line into words. Double and single quotes group words;
// a backslash escapes the next character outside single quotes.
bool tokenize(const std::string& line, std::vector<std::string>& args, std::string& error) {
  std::string cur;
  bool inWord = false;
  char quote = 0;
  for (std::size_t i = 0; i < line.size(); ++i) {
    char ch = line[i];
    if (quote) {
      if (ch == quote) {
        quote = 0;
      } else if (ch == '\\' && quote == '"' && i + 1 < line.size()) {
        cur += line[++i];
      } else {
        cur += ch;
      }
    } else if (ch == '"' || ch == '\'') {
      quote = ch;
      inWord = true;
    } else if (ch == '\\' && i + 1 < line.size()) {
      cur += line[++i];
      inWord = true;
    } else if (std::isspace(static_cast<unsigned char>(ch))) {
      if (inWord) args.push_back(cur);
      cur.clear();
      inWord = false;
    } else {
      cur += ch;
      inWord = true;
    }
  }
  if (quote) {
    error = "a quote was opened but never closed";
    return false;
  }
  if (inWord) args.push_back(cur);
  return true;
}

std::vector<std::string> commandNames() {
  std::vector<std::string> names;
  for (const CommandInfo& c : kCommands) names.push_back(c.name);
  names.push_back("sudo");
  std::sort(names.begin(), names.end());
  return names;
}

}  // namespace

// ------------------------------------------------------------------------ Term

struct Term::Impl {
  Json site;
  TermOptions options;
  Node root;
};

Term::Term(const Json& site, TermOptions options) : impl_(new Impl{site, std::move(options), {}}) {
  impl_->root = buildTree(Site{impl_->site});
}

Term::~Term() { delete impl_; }

Json Term::exec(const std::string& line, const std::string& cwd, const std::vector<std::string>& history,
                long long now) const {
  Ctx c{impl_->site, impl_->options, impl_->root, cwdSegments(cwd), history, now, {}, false};

  std::vector<std::string> args;
  std::string error;
  if (line.size() > kMaxLineChars) {
    c.out.error("Command too long.");
  } else if (!tokenize(line, args, error)) {
    c.out.error("Could not read that: " + error + ".");
  } else if (!args.empty()) {
    std::string name = lower(args[0]);
    args.erase(args.begin());
    auto it = handlers().find(name);
    if (it != handlers().end()) {
      it->second(c, args);
    } else {
      c.out.error("command not found: " + lower(name));
      std::string best;
      std::size_t bestDist = 3;
      for (const std::string& n : commandNames()) {
        std::size_t d = editDistance(lower(name), n);
        if (d < bestDist) bestDist = d, best = n;
      }
      Line hint = {S("Type "), CMD("help"), S(" to see what you can do.")};
      if (!best.empty()) hint = {S("Did you mean "), CMD(best, best == "sudo" ? "help" : best), S("? Or type "), CMD("help"), S(".")};
      c.out.add(hint);
    }
  }

  if (c.out.truncated) c.out.lines.push_back({S("Output truncated at " + std::to_string(kMaxOutputLines) + " lines.", "dim")});
  Json out = Json::object();
  out.set("lines", linesToJson(c.out.lines));
  Json actions = Json::array();
  for (const Json& a : c.out.actions) actions.push(a);
  out.set("actions", std::move(actions));
  out.set("cwd", display(c.cwd));
  return out;
}

Json Term::complete(const std::string& line, const std::string& cwd) const {
  // The word being completed starts after the last space.
  std::size_t space = line.find_last_of(' ');
  std::size_t start = space == std::string::npos ? 0 : space + 1;
  std::string word = line.substr(start);
  Json options = Json::array();
  auto offer = [&](const std::string& s) { options.push(s); };

  std::vector<std::string> before;
  for (const std::string& w : split(line.substr(0, start), ' ')) {
    if (!w.empty()) before.push_back(w);
  }

  auto words = [&](const std::vector<std::string>& list) {
    for (const std::string& w : list) {
      if (w.rfind(lower(word), 0) == 0) offer(w);
    }
  };

  if (before.empty()) {
    words(commandNames());
  } else {
    std::string cmd = lower(before[0]);
    if (cmd == "help") {
      words(commandNames());
    } else if (cmd == "mode") {
      words(impl_->options.modes);
    } else if (cmd == "metar") {
      std::vector<std::string> stations;
      for (const auto& m : impl_->options.metars) stations.push_back(lower(m.first));
      words(stations);
    } else if (cmd == "sudo") {
      words({"hire"});
    } else if (cmd == "hire") {
      words({"franco"});
    } else if (cmd == "open") {
      std::vector<std::string> targets = {"github", "linkedin", "resume"};
      for (const Json& p : impl_->site.at("projects").items()) {
        if (p.at("repo").isString()) targets.push_back(projectSlug(p));
      }
      words(targets);
    } else if (cmd == "cd" || cmd == "ls" || cmd == "cat" || cmd == "tree") {
      bool dirsOnly = cmd == "cd" || cmd == "tree";
      std::size_t slash = word.find_last_of('/');
      std::string dirPart = slash == std::string::npos ? "" : word.substr(0, slash + 1);
      std::string prefix = slash == std::string::npos ? word : word.substr(slash + 1);
      if (word == "~") {
        offer("~/");
      } else {
        Path dir;
        const Node* d = resolve(cwdSegments(cwd), dirPart, dir) ? nodeAt(impl_->root, dir) : nullptr;
        if (d && d->kind == Node::Kind::Dir) {
          for (const Node& n : d->children) {
            if (dirsOnly && n.kind != Node::Kind::Dir) continue;
            if (lower(n.name).rfind(lower(prefix), 0) != 0) continue;
            if (prefix.empty() && n.name[0] == '.') continue;
            offer(dirPart + n.name + (n.kind == Node::Kind::Dir ? "/" : ""));
          }
        }
      }
    }
  }

  Json out = Json::object();
  out.set("start", static_cast<double>(start));
  out.set("options", std::move(options));
  return out;
}

namespace {

// A one-line ASCII sparkline of the daily closes (8 levels, at most `width` characters).
std::string sparkline(const std::vector<Candle>& series, std::size_t width) {
  static const char ramp[] = "_.-=+*#@";
  if (series.empty()) return "";
  std::vector<double> v;
  for (std::size_t i = 0; i < std::min(width, series.size()); ++i) {
    std::size_t at = series.size() <= width ? i : i * (series.size() - 1) / (width - 1);
    v.push_back(series[at].c);
  }
  double lo = *std::min_element(v.begin(), v.end()), hi = *std::max_element(v.begin(), v.end());
  std::string out;
  for (double x : v) {
    int level = hi - lo < 1e-9 ? 3 : static_cast<int>(std::lround((x - lo) / (hi - lo) * 7.0));
    out += ramp[std::max(0, std::min(7, level))];
  }
  return out;
}

constexpr std::size_t kStatsBlockLines = 12;

// The bot's status block. With `placeholder` (or when the feed failed) the numbers read [+x.x%].
Out statsBlock(const Json& raw, bool placeholder, const std::string& trackingSince, const Json& history, bool padToBlock) {
  Out out;
  bool live = !placeholder && raw.isObject();
  Json n = normalizeStats(live ? raw : Json::object(), trackingSince);
  out.text("AutoTradingBot, paper trading, since " + n.at("startDate").asString(), "accent");
  for (const Json& t : n.at("tiles").items()) {
    bool hasValue = live && t.at("value").isNumber();
    std::string tone = t.at("tone").asString();
    out.add({S("  " + pad(t.at("label").asString(), 14), "dim"),
             S(hasValue ? t.at("text").asString() : (t.at("id").asString() == "max_drawdown" ? "[-x.x%]" : "[+x.x%]"), hasValue ? (tone == "pos" ? "pos" : tone == "neg" ? "neg" : "") : "dim")});
  }
  if (placeholder) {
    out.text("Waking up the bot. The free host can take about a minute.", "dim");
  } else if (!raw.isObject()) {
    out.text("The bot did not answer. Its free host sleeps when idle; try again in a moment.", "dim");
  } else if (!n.at("note").asString().empty()) {
    out.text(n.at("note").asString(), "dim");
  } else {
    out.blank();
  }
  out.text(std::string(live && !n.at("asOf").asString().empty() ? "Updated " + n.at("asOf").asString() + ". " : "") +
               "Percentages only. Paper trading, not investment advice.",
           "dim");

  std::vector<Candle> series;
  std::string err;
  bool liveSeries = history.isArray() && parseHistory(history, series, err);
  if (!liveSeries) series = illustrativeSeries(7, 30);
  out.add({S("  "), S(sparkline(series, 30), "accent")});
  out.text(liveSeries ? "Bot close by day, since " + series.front().label + ", indexed to 100."
                      : "Illustrative data, not real results. The real history appears once the bot publishes it.",
           liveSeries ? "dim" : "warn");
  while (padToBlock && out.lines.size() < kStatsBlockLines) out.blank();
  return out;
}

}  // namespace

Json Term::statsOutput(const Json& raw, const std::string& trackingSince, const Json& history, bool padToBlock) const {
  Out out = statsBlock(raw, false, trackingSince, history, padToBlock);
  Json res = Json::object();
  res.set("lines", linesToJson(out.lines));
  return res;
}

Json Term::statsPlaceholder(const std::string& trackingSince, bool padToBlock) const {
  Out out = statsBlock(Json(), true, trackingSince, Json(), padToBlock);
  Json res = Json::object();
  res.set("lines", linesToJson(out.lines));
  return res;
}

Json Term::files() const {
  std::vector<std::pair<std::string, const Node*>> all;
  collectFiles(impl_->root, {}, all);
  Json out = Json::array();
  for (const auto& f : all) {
    Json o = Json::object();
    o.set("path", f.first);
    o.set("content", f.second->content);
    out.push(std::move(o));
  }
  return out;
}

}  // namespace fc
