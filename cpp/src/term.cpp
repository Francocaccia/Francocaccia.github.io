#include "fc/term.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <map>

#include "fc/stats.hpp"
#include "fc/weather.hpp"

namespace fc {

namespace {

constexpr const char* kUser = "visitor";
constexpr const char* kHome = "/home/franco";
constexpr std::size_t kCols = 44;  // layout width that still fits a 390px phone

// ---------------------------------------------------------------- output model

struct Span {
  std::string text, kind, href, cmd;
};
using Line = std::vector<Span>;

Span S(std::string text, std::string kind = "") { return {std::move(text), std::move(kind), "", ""}; }

// Only these schemes may become links; anything else stays plain text.
bool safeHref(const std::string& h) {
  if (h.rfind("https://", 0) == 0 || h.rfind("mailto:", 0) == 0) return true;
  return h.size() > 1 && h[0] == '/' && h[1] != '/';
}

Span LINK(std::string label, std::string href) {
  if (!safeHref(href)) return S(std::move(label));
  return {std::move(label), "link", std::move(href), ""};
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

  void add(Line l) { lines.push_back(std::move(l)); }
  void text(const std::string& s, const std::string& kind = "") { lines.push_back({S(s, kind)}); }
  void blank() { lines.emplace_back(); }
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
  return t;
}

std::string certsText(const Site& s) {
  std::string t = "# Licenses and certifications\n\n";
  for (const Json& c : s.j.at("certifications").items()) {
    std::string status = str(c, "status");
    if (status == "in-progress") status = "in progress";
    t += pad("[" + status + "]", 14) + str(c, "name");
    if (!str(c, "issuer").empty()) t += " (" + str(c, "issuer") + ")";
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
  t += "Status: " + str(p, "statusLabel") + "\n\n" + str(p, "summary") + "\n\n";
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
  return "# " + str(e, "role") + "\n" + str(e, "org") + ", " + str(e, "start") + " to " + str(e, "end") + "\n\n" +
         str(e, "description") + "\n";
}

std::string educationText(const Site& s) {
  std::string t = "# Education\n\n";
  for (const Json& e : s.j.at("education").items()) {
    t += str(e, "degree") + "\n" + str(e, "school") + ", " + orTodo(str(e, "period"), "years") + "\n\n";
  }
  return t;
}

std::string projectSlug(const Json& p) { return str(p, "id"); }

Node buildTree(const Site& s) {
  Node root = makeDir("~");
  root.children.push_back(makeFile("about.md", aboutText(s)));
  root.children.push_back(makeFile("skills.txt", skillsText(s)));
  root.children.push_back(makeFile("certs.txt", certsText(s)));
  root.children.push_back(makeFile("contact.txt", contactText(s)));

  Node resume;
  resume.kind = Node::Kind::Link;
  resume.name = "resume";
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
    std::string id = str(e, "id");
    if (id.rfind("exp-", 0) == 0) id = id.substr(4);
    experience.children.push_back(makeFile(id + ".md", roleText(e)));
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
    {"grep", "Look around", "grep <text>", "Search every file for a word or phrase.", "grep python"},
    {"whoami", "About me", "whoami", "One line about me.", "whoami"},
    {"neofetch", "About me", "neofetch", "A quick facts card.", "neofetch"},
    {"projects", "About me", "projects", "List my projects.", "projects"},
    {"stats", "About me", "stats", "Live status of the trading bot (percentages only).", "stats"},
    {"open", "Go somewhere", "open <github|linkedin|resume|project>", "Print a clickable link.", "open github"},
    {"mode", "Go somewhere", "mode <finance|aviation>", "Switch to another view of this site.", "mode finance"},
    {"metar", "Go somewhere", "metar <sabe|kabe>", "Decode the weather report for an airport.", "metar kabe"},
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
    std::string command = (n.kind == Node::Kind::Dir ? "ls " : "cat ") + display(full);
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

void cmdLs(Ctx& c, const std::vector<std::string>& a) {
  std::vector<std::string> targets;
  for (const std::string& x : a) {
    if (x.empty() || x[0] != '-') targets.push_back(x);  // flags like -l are accepted and ignored
  }
  if (targets.empty()) targets.push_back("");
  for (std::size_t i = 0; i < targets.size(); ++i) {
    Path p;
    const Node* n = resolve(c.cwd, targets[i], p) ? nodeAt(c.root, p) : nullptr;
    if (!n) {
      c.out.error("ls: cannot find '" + targets[i] + "'. Try: ls");
      continue;
    }
    if (targets.size() > 1) c.out.text(targets[i] + ":", "dim");
    if (n->kind == Node::Kind::Dir) {
      listDir(c, *n, p);
    } else {
      c.out.add({CMD(n->name, "cat " + display(p))});
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
  const Node* n = nodeAt(c.root, p);
  if (!n) {
    c.out.error("cd: no such folder '" + arg + "'. Try: ls");
    return;
  }
  if (n->kind != Node::Kind::Dir) {
    c.out.error("cd: '" + arg + "' is a file, not a folder.");
    c.out.add({S("Read it with: ", "dim"), CMD("cat " + arg)});
    return;
  }
  c.cwd = p;
  c.cwdChanged = true;
}

void cmdPwd(Ctx& c, const std::vector<std::string>&) {
  c.out.text(std::string(kHome) + (c.cwd.empty() ? "" : "/" + join(c.cwd, "/")));
}

void printFile(Ctx& c, const Node& n, const Path& p) {
  if (n.kind == Node::Kind::Link) {
    if (n.href.empty()) {
      c.out.text(n.content, "dim");
    } else {
      c.out.add({S("Resume: ", "dim"), LINK(n.href, n.href)});
    }
    return;
  }
  (void)p;
  std::string cur;
  for (std::size_t i = 0; i <= n.content.size(); ++i) {
    if (i == n.content.size() || n.content[i] == '\n') {
      if (i == n.content.size() && cur.empty()) break;
      if (cur.rfind("# ", 0) == 0) {
        c.out.text(cur.substr(2), "accent");
      } else {
        c.out.add(linkify(cur));
      }
      cur.clear();
    } else {
      cur += n.content[i];
    }
  }
}

void cmdCat(Ctx& c, const std::vector<std::string>& a) {
  if (a.empty()) return usage(c, "cat");
  for (std::size_t i = 0; i < a.size(); ++i) {
    Path p;
    const Node* n = resolve(c.cwd, a[i], p) ? nodeAt(c.root, p) : nullptr;
    if (!n) {
      c.out.error("cat: no such file '" + a[i] + "'. Try: ls");
      continue;
    }
    if (n->kind == Node::Kind::Dir) {
      c.out.error("cat: '" + a[i] + "' is a folder, not a file.");
      c.out.add({S("List it with: ", "dim"), CMD("ls " + a[i])});
      continue;
    }
    if (a.size() > 1) c.out.text("--- " + a[i], "dim");
    printFile(c, *n, p);
  }
}

void drawTree(const Node& dir, const std::string& prefix, std::vector<Line>& lines) {
  for (std::size_t i = 0; i < dir.children.size(); ++i) {
    const Node& n = dir.children[i];
    bool last = i + 1 == dir.children.size();
    std::string label = n.name + (n.kind == Node::Kind::Dir ? "/" : n.kind == Node::Kind::Link ? "@" : "");
    lines.push_back({S(prefix + (last ? "`-- " : "|-- ")), S(label, n.kind == Node::Kind::Dir ? "accent" : "")});
    if (n.kind == Node::Kind::Dir) drawTree(n, prefix + (last ? "    " : "|   "), lines);
  }
}

void cmdTree(Ctx& c, const std::vector<std::string>& a) {
  Path p;
  std::string arg = a.empty() ? "" : a[0];
  const Node* n = resolve(c.cwd, arg, p) ? nodeAt(c.root, p) : nullptr;
  if (!n || n->kind != Node::Kind::Dir) {
    c.out.error("tree: no such folder '" + arg + "'.");
    return;
  }
  c.out.text(display(p), "accent");
  std::vector<Line> lines;
  drawTree(*n, "", lines);
  for (Line& l : lines) c.out.add(std::move(l));
}

void cmdGrep(Ctx& c, const std::vector<std::string>& a) {
  if (a.empty()) return usage(c, "grep");
  std::string needle = lower(join(a, " "));
  std::vector<std::pair<std::string, const Node*>> files;
  collectFiles(c.root, {}, files);
  constexpr std::size_t kMaxHits = 30;
  std::size_t hits = 0, filesHit = 0;
  for (const auto& f : files) {
    bool any = false;
    std::size_t lineNo = 0;
    for (const std::string& line : split(f.second->content, '\n')) {
      ++lineNo;
      std::size_t pos = lower(line).find(needle);
      if (pos == std::string::npos) continue;
      if (!any) ++filesHit;
      any = true;
      if (++hits > kMaxHits) continue;
      std::size_t from = pos > 24 ? pos - 24 : 0;
      std::size_t to = std::min(line.size(), pos + needle.size() + 32);
      Line row = {CMD(f.first, "cat " + f.first), S(":" + std::to_string(lineNo) + ": ", "dim")};
      if (from > 0) row.push_back(S("...", "dim"));
      row.push_back(S(line.substr(from, pos - from)));
      row.push_back(S(line.substr(pos, needle.size()), "match"));
      row.push_back(S(line.substr(pos + needle.size(), to - pos - needle.size())));
      if (to < line.size()) row.push_back(S("...", "dim"));
      c.out.add(std::move(row));
    }
  }
  if (hits == 0) {
    c.out.text("No matches for '" + join(a, " ") + "'. Try a shorter word, for example: grep go", "dim");
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

// "2026-10-18T15:00:00.000Z" -> "2026-10-18 15:00 UTC"
std::string utcStamp(const std::string& iso) {
  if (iso.size() >= 16 && iso[10] == 'T') return iso.substr(0, 10) + " " + iso.substr(11, 5) + " UTC";
  return iso;
}

void cmdMetar(Ctx& c, const std::vector<std::string>& a) {
  if (a.empty()) return usage(c, "metar");
  std::string st = lower(a[0]);
  if (st != "sabe" && st != "kabe") {
    c.out.error("metar: pick sabe (Buenos Aires) or kabe (Lehigh Valley).");
    return;
  }
  auto found = c.options.metars.find(st == "sabe" ? "SABE" : "KABE");
  bool aviation = std::find(c.options.modes.begin(), c.options.modes.end(), "aviation") != c.options.modes.end();
  if (found == c.options.metars.end()) {
    c.out.text("METAR decoding is coming soon. It will live in the Aviation view.", "dim");
    if (aviation) c.out.add({S("Take a look: ", "dim"), CMD("mode aviation")});
    return;
  }
  Json d = decodeMetar(found->second.raw, utcStamp(found->second.observedAt));
  if (!d.at("ok").asBool()) {
    c.out.error("metar: " + d.at("error").asString());
    return;
  }
  c.out.add({S(found->second.raw, "accent")});
  c.out.add({S("Observed " + utcStamp(found->second.observedAt), "dim"),
             S(found->second.sample ? "  (sample data, not a live report)" : "", "dim")});
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
  if (!tokenize(line, args, error)) {
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
      words({"sabe", "kabe"});
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

Json Term::statsOutput(const Json& raw, const std::string& trackingSince) const {
  Out out;
  if (!raw.isObject()) {
    out.error("The bot did not answer.");
    out.text("Its free host sleeps when idle and can take about a minute to wake. Try again in a moment.", "dim");
  } else {
    Json n = normalizeStats(raw, trackingSince);
    out.text("AutoTradingBot, paper trading, since " + n.at("startDate").asString(), "accent");
    for (const Json& t : n.at("tiles").items()) {
      std::string tone = t.at("tone").asString();
      out.add({S("  " + pad(t.at("label").asString(), 14), "dim"),
               S(t.at("text").asString(), tone == "pos" ? "pos" : tone == "neg" ? "neg" : "")});
    }
    if (!n.at("note").asString().empty()) out.text(n.at("note").asString(), "dim");
    out.text(std::string(n.at("asOf").asString().empty() ? "" : "Updated " + n.at("asOf").asString() + ". ") +
                 "Percentages only. Paper trading, not investment advice.",
             "dim");
  }
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
