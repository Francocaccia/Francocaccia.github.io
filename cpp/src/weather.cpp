#include "fc/weather.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

#include "weather_internal.hpp"

namespace fc {
namespace wx {

// ------------------------------------------------------------------ text helpers

std::string esc(const std::string& s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += c;
    }
  }
  return out;
}

std::string fmt(double v, int decimals) {
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
  std::string s = buf;
  if (s == "-0" || s == "-0.0" || s == "-0.00") s = s.substr(1);
  return s;
}

std::string thousands(long v) {
  std::string digits = std::to_string(std::labs(v));
  std::string out;
  for (std::size_t i = 0; i < digits.size(); ++i) {
    if (i && (digits.size() - i) % 3 == 0) out += ',';
    out += digits[i];
  }
  return (v < 0 ? "-" : "") + out;
}

std::string upperOf(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return s;
}

std::string cap(std::string s) {
  if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
  return s;
}

std::string two(int v) { return (v < 10 ? "0" : "") + std::to_string(v); }

std::vector<std::string> words(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (!cur.empty()) out.push_back(cur), cur.clear();
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

bool digitsAt(const std::string& s, std::size_t pos, std::size_t n) {
  if (pos + n > s.size()) return false;
  for (std::size_t i = 0; i < n; ++i) {
    if (!std::isdigit(static_cast<unsigned char>(s[pos + i]))) return false;
  }
  return true;
}

int intAt(const std::string& s, std::size_t pos, std::size_t n) { return std::atoi(s.substr(pos, n).c_str()); }

const char* compass(int degrees) {
  static const char* const names[] = {"north",     "north-northeast", "northeast", "east-northeast", "east",      "east-southeast",
                                      "southeast", "south-southeast", "south",     "south-southwest", "southwest", "west-southwest",
                                      "west",      "west-northwest",  "northwest", "north-northwest"};
  int i = static_cast<int>(std::lround(degrees / 22.5)) % 16;
  return names[(i + 16) % 16];
}

// -------------------------------------------------------------------------- wind

double toKt(double v, const std::string& unit) {
  if (unit == "MPS") return v * kKtPerMs;
  if (unit == "KMH") return v / 1.852;
  return v;
}

int ktOf(const Wind& w, bool gust) { return static_cast<int>(std::lround(toKt(gust ? w.gust : w.speed, w.unit))); }

static bool parseWind(const std::string& t, Wind& w) {
  std::string unit;
  if (t.size() >= 5 && t.compare(t.size() - 2, 2, "KT") == 0) unit = "KT";
  else if (t.size() >= 6 && t.compare(t.size() - 3, 3, "MPS") == 0) unit = "MPS";
  else if (t.size() >= 6 && t.compare(t.size() - 3, 3, "KMH") == 0) unit = "KMH";
  else return false;
  std::string body = t.substr(0, t.size() - unit.size());
  Wind r;
  r.unit = unit;
  if (body == "/////") {
    r.missing = r.present = true;
    w = r;
    return true;
  }
  std::size_t pos = 0;
  if (body.rfind("VRB", 0) == 0) {
    r.variable = true;
    pos = 3;
  } else if (digitsAt(body, 0, 3)) {
    r.dir = intAt(body, 0, 3);
    if (r.dir > 360) return false;
    pos = 3;
  } else {
    return false;
  }
  std::size_t sp = pos;
  while (sp < body.size() && std::isdigit(static_cast<unsigned char>(body[sp]))) ++sp;
  if (sp - pos < 2 || sp - pos > 3) return false;
  r.speed = intAt(body, pos, sp - pos);
  if (sp < body.size()) {
    if (body[sp] != 'G') return false;
    std::size_t gl = body.size() - sp - 1;
    if (gl < 2 || gl > 3 || !digitsAt(body, sp + 1, gl)) return false;
    r.gust = intAt(body, sp + 1, gl);
  }
  r.present = true;
  r.calm = !r.variable && r.dir == 0 && r.speed == 0;
  w = r;
  return true;
}

std::string windText(const Wind& w) {
  if (!w.present || w.missing) return "Not reported";
  if (w.calm) return "Calm";
  int kt = ktOf(w);
  double ms = toKt(w.speed, w.unit) / kKtPerMs;
  double kmh = toKt(w.speed, w.unit) * 1.852;
  std::string s = w.variable ? "Variable direction" : "From " + std::to_string(w.dir) + "\xC2\xB0 (" + compass(w.dir) + ")";
  s += " at " + std::to_string(kt) + " kt (" + fmt(ms, 1) + " m/s, " + fmt(kmh, 0) + " km/h)";
  if (w.gust >= 0) s += ", gusting " + std::to_string(ktOf(w, true)) + " kt";
  if (w.varFrom >= 0) s += ", varying between " + std::to_string(w.varFrom) + "\xC2\xB0 and " + std::to_string(w.varTo) + "\xC2\xB0";
  return s;
}

// -------------------------------------------------------------------- visibility

static std::string visText(const Vis& v) {
  if (!v.set) return "Not reported";
  double sm = v.meters / kMPerSm;
  if (v.text == "9999") return "10 km or more";
  std::string metric = v.meters >= 5000 ? fmt(v.meters / 1000.0, v.meters >= 10000 ? 0 : 1) + " km" : thousands(std::lround(v.meters)) + " m";
  bool whole = sm >= 10 || std::fabs(sm - std::round(sm)) < 0.05;
  std::string imperial = whole ? fmt(sm, 0) + " SM" : fmt(sm, sm < 1 ? 2 : 1) + " SM";
  std::string qualifier = v.more ? "More than " : v.less ? "Less than " : "";
  // Show the unit the report used first.
  if (v.text.find("SM") != std::string::npos) return qualifier + imperial + " (" + metric + ")";
  return qualifier + metric + " (" + imperial + ")";
}

// Visibility in statute miles: "9999", "0800", "10SM", "1/2SM", "P6SM", "M1/4SM", or "1" + "1/2SM".
static bool parseVisibility(const std::vector<std::string>& tk, std::size_t i, Vis& v, std::size_t& used) {
  const std::string& t = tk[i];
  if (t.size() == 4 && digitsAt(t, 0, 4)) {
    v.set = true;
    v.meters = intAt(t, 0, 4);
    v.text = t;
    if (t == "9999") v.meters = 10000;
    used = 1;
    return true;
  }
  auto smBody = [](const std::string& s, bool& more, bool& less, double& sm) {
    if (s.size() < 3 || s.compare(s.size() - 2, 2, "SM") != 0) return false;
    std::string b = s.substr(0, s.size() - 2);
    more = less = false;
    if (!b.empty() && b[0] == 'P') more = true, b = b.substr(1);
    else if (!b.empty() && b[0] == 'M') less = true, b = b.substr(1);
    std::size_t slash = b.find('/');
    if (slash == std::string::npos) {
      if (b.empty() || !std::all_of(b.begin(), b.end(), [](unsigned char c) { return std::isdigit(c); })) return false;
      sm = std::atof(b.c_str());
    } else {
      std::string n = b.substr(0, slash), d = b.substr(slash + 1);
      if (n.empty() || d.empty() || !std::all_of(n.begin(), n.end(), [](unsigned char c) { return std::isdigit(c); }) ||
          !std::all_of(d.begin(), d.end(), [](unsigned char c) { return std::isdigit(c); }) || std::atof(d.c_str()) == 0) {
        return false;
      }
      sm = std::atof(n.c_str()) / std::atof(d.c_str());
    }
    return true;
  };
  bool more, less;
  double sm;
  // "1 1/2SM": a whole number followed by a fraction
  if (t.size() <= 2 && digitsAt(t, 0, t.size()) && i + 1 < tk.size() && tk[i + 1].find('/') != std::string::npos &&
      smBody(tk[i + 1], more, less, sm)) {
    v.set = true;
    v.meters = (std::atof(t.c_str()) + sm) * kMPerSm;
    v.text = t + " " + tk[i + 1];
    used = 2;
    return true;
  }
  if (smBody(t, more, less, sm)) {
    v.set = true;
    v.more = more;
    v.less = less;
    v.meters = sm * kMPerSm;
    v.text = t;
    used = 1;
    return true;
  }
  return false;
}

// ------------------------------------------------------------------------- weather

static bool parseWx(const std::string& tok, std::string& phrase) {
  static const std::map<std::string, std::string> descriptors = {
      {"MI", "shallow"}, {"PR", "partial"}, {"BC", "patches of"}, {"DR", "low drifting"},
      {"BL", "blowing"}, {"SH", "showers"}, {"TS", "thunderstorm"}, {"FZ", "freezing"}};
  static const std::map<std::string, std::string> phenomena = {
      {"DZ", "drizzle"}, {"RA", "rain"},  {"SN", "snow"},  {"SG", "snow grains"}, {"IC", "ice crystals"},
      {"PL", "ice pellets"}, {"GR", "hail"}, {"GS", "small hail"}, {"UP", "unknown precipitation"},
      {"BR", "mist"},    {"FG", "fog"},   {"FU", "smoke"}, {"VA", "volcanic ash"}, {"DU", "widespread dust"},
      {"SA", "sand"},    {"HZ", "haze"},  {"PY", "spray"}, {"PO", "dust whirls"}, {"SQ", "squalls"},
      {"FC", "funnel cloud"}, {"SS", "sandstorm"}, {"DS", "duststorm"}};
  if (tok.empty()) return false;
  if (tok == "+FC") {
    phrase = "Tornado or waterspout";
    return true;
  }
  std::size_t p = 0;
  std::string intensity;
  if (tok[0] == '-') intensity = "light", p = 1;
  else if (tok[0] == '+') intensity = "heavy", p = 1;
  bool vicinity = false;
  if (tok.compare(p, 2, "VC") == 0) vicinity = true, p += 2;
  std::string rest = tok.substr(p);
  if (rest.size() < 2 || rest.size() % 2 != 0) return false;
  bool ts = false, sh = false, fz = false;
  std::string prefix;
  std::vector<std::string> phen;
  for (std::size_t i = 0; i < rest.size(); i += 2) {
    std::string code = rest.substr(i, 2);
    auto d = descriptors.find(code);
    if (d != descriptors.end()) {
      if (code == "TS") ts = true;
      else if (code == "SH") sh = true;
      else if (code == "FZ") fz = true;
      else prefix += d->second + " ";
      continue;
    }
    auto ph = phenomena.find(code);
    if (ph == phenomena.end()) return false;
    phen.push_back(ph->second);
  }
  std::string phenText;
  for (std::size_t i = 0; i < phen.size(); ++i) phenText += (i == 0 ? "" : i + 1 == phen.size() ? " and " : ", ") + phen[i];
  std::string base;
  if (ts) base = phen.empty() ? "thunderstorm" : "thunderstorm with " + phenText;
  else if (sh) base = phen.empty() ? "showers" : phenText + " showers";
  else base = phenText;
  if (fz) base = base.empty() ? "freezing" : "freezing " + base;
  base = prefix + base;
  if (!intensity.empty()) base = intensity + " " + base;
  if (vicinity) base += " in the vicinity";
  while (!base.empty() && base.back() == ' ') base.pop_back();
  phrase = cap(base);
  return true;
}

// ------------------------------------------------------------------------- clouds

static const char* coverWord(const std::string& c) {
  if (c == "FEW") return "Few clouds (1-2 oktas)";
  if (c == "SCT") return "Scattered clouds (3-4 oktas)";
  if (c == "BKN") return "Broken clouds (5-7 oktas)";
  if (c == "OVC") return "Overcast (8 oktas)";
  return "Sky obscured";
}

std::string cloudText(const Cloud& c) {
  std::string s = coverWord(c.cover);
  if (c.cover == "VV") {
    s += c.hundreds >= 0 ? "; vertical visibility " + thousands(c.hundreds * 100L) + " ft" : "; vertical visibility not reported";
    return s;
  }
  if (c.hundreds >= 0) {
    long ft = c.hundreds * 100L;
    s += " at " + thousands(ft) + " ft AGL (" + thousands(std::lround(ft * 0.3048 / 10.0) * 10) + " m)";
  } else {
    s += ", height not reported";
  }
  if (c.kind == "CB") s += ", cumulonimbus";
  if (c.kind == "TCU") s += ", towering cumulus";
  return s;
}

static bool parseCloud(const std::string& t, Cloud& c) {
  for (const char* cover : {"FEW", "SCT", "BKN", "OVC"}) {
    if (t.rfind(cover, 0) != 0) continue;
    std::string h = t.substr(3, 3), kind = t.size() > 6 ? t.substr(6) : "";
    if (t.size() < 6 || (!digitsAt(t, 3, 3) && h != "///")) return false;
    if (!kind.empty() && kind != "CB" && kind != "TCU" && kind != "///") return false;
    c.cover = cover;
    c.hundreds = h == "///" ? -1 : intAt(t, 3, 3);
    c.kind = kind == "///" ? "" : kind;
    return true;
  }
  if (t.rfind("VV", 0) == 0 && t.size() == 5 && (digitsAt(t, 2, 3) || t.substr(2) == "///")) {
    c.cover = "VV";
    c.hundreds = t.substr(2) == "///" ? -1 : intAt(t, 2, 3);
    return true;
  }
  return false;
}

int ceilingFt(const Elements& e) {
  int ceiling = -1;
  for (const Cloud& c : e.clouds) {
    if ((c.cover == "BKN" || c.cover == "OVC" || c.cover == "VV") && c.hundreds >= 0) {
      int ft = c.hundreds * 100;
      ceiling = ceiling < 0 ? ft : std::min(ceiling, ft);
    }
  }
  return ceiling;
}

// FAA flight categories from the lower of ceiling and visibility.
Category categoryOf(const Elements& e) {
  if (e.cavok) return {"VFR", "CAVOK: visibility 10 km or more and no significant cloud."};
  int ceiling = ceilingFt(e);
  bool haveVis = e.vis.set;
  if (!haveVis && ceiling < 0 && e.clouds.empty() && !e.noCloud) return {"Unknown", "Visibility and cloud were not reported."};
  double sm = e.vis.meters / kMPerSm;
  int rv = !haveVis ? 0 : sm < 1 ? 3 : sm < 3 ? 2 : sm <= 5 ? 1 : 0;
  int rc = ceiling < 0 ? 0 : ceiling < 500 ? 3 : ceiling < 1000 ? 2 : ceiling <= 3000 ? 1 : 0;
  static const char* const names[] = {"VFR", "MVFR", "IFR", "LIFR"};
  int worst = std::max(rv, rc);
  std::string visPart = haveVis ? "visibility " + fmt(sm, sm < 10 ? 1 : 0) + " SM" : "visibility not reported";
  std::string ceilPart = ceiling < 0 ? "no ceiling" : "ceiling " + thousands(ceiling) + " ft";
  std::string why;
  if (worst == 0) why = "Ceiling above 3,000 ft (or none) and visibility above 5 SM.";
  else if (rv == rc) why = cap(ceilPart) + " and " + visPart + ".";
  else why = cap(rc == worst ? ceilPart : visPart) + " sets the category.";
  return {names[worst], why};
}

// ------------------------------------------------------------------ element reader

bool parseElement(const std::vector<std::string>& tk, std::size_t i, Elements& e, Group& g, std::size_t& used) {
  const std::string& t = tk[i];
  used = 1;

  Wind w;
  if (parseWind(t, w)) {
    e.wind = w;
    g = {t, "wind", "Wind: " + windText(w)};
    return true;
  }
  if (t.size() == 7 && digitsAt(t, 0, 3) && t[3] == 'V' && digitsAt(t, 4, 3) && e.wind.present) {
    e.wind.varFrom = intAt(t, 0, 3);
    e.wind.varTo = intAt(t, 4, 3);
    g = {t, "windvar", "Wind direction varying between " + std::to_string(e.wind.varFrom) + "\xC2\xB0 and " + std::to_string(e.wind.varTo) + "\xC2\xB0"};
    return true;
  }
  if (t == "CAVOK") {
    e.cavok = true;
    g = {t, "vis", "CAVOK: visibility 10 km or more, no significant cloud, no significant weather"};
    return true;
  }
  Vis v;
  std::size_t consumed = 1;
  if (parseVisibility(tk, i, v, consumed)) {
    e.vis = v;
    used = consumed;
    std::string text = tk[i];
    if (consumed == 2) text += " " + tk[i + 1];
    g = {text, "vis", "Visibility: " + visText(v)};
    return true;
  }
  // Directional minimum visibility, e.g. 1200NE
  if (t.size() >= 5 && digitsAt(t, 0, 4) && t.find_first_not_of("NESW", 4) == std::string::npos) {
    g = {t, "vis", "Lowest visibility " + fmt(intAt(t, 0, 4), 0) + " m toward the " + t.substr(4)};
    return true;
  }
  if (t == "NDV") {
    g = {t, "vis", "No directional variation in visibility"};
    return true;
  }
  if (t == "NSW") {
    e.nsw = true;
    g = {t, "wx", "No significant weather"};
    return true;
  }
  std::string phrase;
  if (parseWx(t, phrase)) {
    e.wx.push_back(phrase);
    g = {t, "wx", "Weather: " + phrase};
    return true;
  }
  Cloud c;
  if (parseCloud(t, c)) {
    e.clouds.push_back(c);
    g = {t, "cloud", cloudText(c)};
    return true;
  }
  if (t == "SKC" || t == "CLR" || t == "NSC" || t == "NCD") {
    e.noCloud = true;
    e.noCloudText = t == "SKC" ? "Sky clear" : t == "CLR" ? "Clear below 12,000 ft (automated station)"
                  : t == "NSC" ? "No significant cloud" : "No cloud detected (automated station)";
    g = {t, "cloud", e.noCloudText};
    return true;
  }
  // Runway visual range: R26L/0600V1000N, R06/P2000, R10/M0050FT
  if (t.size() >= 7 && t[0] == 'R' && digitsAt(t, 1, 2)) {
    std::size_t slash = t.find('/');
    if (slash != std::string::npos && slash >= 3 && slash <= 4) {
      std::string rwy = t.substr(1, slash - 1), val = t.substr(slash + 1);
      bool feet = val.find("FT") != std::string::npos;
      std::string trendLetter;
      if (!val.empty() && (val.back() == 'U' || val.back() == 'D' || val.back() == 'N')) {
        trendLetter = val.substr(val.size() - 1);
        val.pop_back();
        if (!val.empty() && val.back() == '/') val.pop_back();
      }
      if (feet) val = val.substr(0, val.find("FT"));
      std::string unit = feet ? " ft" : " m";
      std::string lo = val, hi;
      std::size_t vpos = val.find('V');
      if (vpos != std::string::npos) lo = val.substr(0, vpos), hi = val.substr(vpos + 1);
      auto number = [&](std::string s) {
        std::string prefix;
        if (!s.empty() && s[0] == 'P') prefix = "more than ", s = s.substr(1);
        else if (!s.empty() && s[0] == 'M') prefix = "less than ", s = s.substr(1);
        return prefix + std::to_string(std::atoi(s.c_str())) + unit;
      };
      if (!lo.empty() && std::all_of(lo.begin(), lo.end(), [](unsigned char ch) { return std::isdigit(ch) || ch == 'P' || ch == 'M'; })) {
        std::string text = "Runway " + rwy + ": visual range " + number(lo);
        if (!hi.empty()) text += " varying to " + number(hi);
        text += trendLetter == "U" ? ", improving" : trendLetter == "D" ? ", worsening" : trendLetter == "N" ? ", no change" : "";
        e.extra.push_back(text);
        g = {t, "rvr", text};
        return true;
      }
    }
  }
  // Wind shear: WS RWY27 / WS ALL RWY / WS020/18040KT (TAF)
  if (t == "WS" && i + 1 < tk.size()) {
    if (tk[i + 1] == "ALL" && i + 2 < tk.size() && tk[i + 2] == "RWY") {
      used = 3;
      e.extra.push_back("Wind shear on all runways");
      g = {"WS ALL RWY", "wx", "Wind shear on all runways"};
      return true;
    }
    if (tk[i + 1].rfind("RWY", 0) == 0) {
      used = 2;
      e.extra.push_back("Wind shear on runway " + tk[i + 1].substr(3));
      g = {"WS " + tk[i + 1], "wx", "Wind shear on runway " + tk[i + 1].substr(3)};
      return true;
    }
  }
  if (t.size() == 12 && t.rfind("WS", 0) == 0 && digitsAt(t, 2, 3) && t[5] == '/') {
    std::string text = "Wind shear at " + thousands(intAt(t, 2, 3) * 100L) + " ft: " + windText([&] {
      Wind sw;
      parseWind(t.substr(6), sw);
      return sw;
    }());
    e.extra.push_back(text);
    g = {t, "wx", text};
    return true;
  }
  return false;
}

std::string describe(const Elements& e) {
  std::vector<std::string> parts;
  if (e.wind.present) parts.push_back("wind: " + windText(e.wind));
  if (e.cavok) parts.push_back("CAVOK (visibility 10 km or more, no significant cloud or weather)");
  if (e.vis.set) parts.push_back("visibility: " + visText(e.vis));
  for (const std::string& w : e.wx) parts.push_back(w);
  if (e.nsw) parts.push_back("no significant weather");
  for (const std::string& x : e.extra) parts.push_back(x);
  for (const Cloud& c : e.clouds) parts.push_back(cloudText(c));
  if (e.noCloud) parts.push_back(e.noCloudText);
  std::string out;
  for (std::size_t i = 0; i < parts.size(); ++i) out += (i ? "; " : "") + (i == 0 ? cap(parts[i]) : parts[i]);
  return out.empty() ? "No change details given" : out + ".";
}

std::string catBadge(const std::string& name) {
  std::string cls = "cat-unknown";
  if (name == "VFR") cls = "cat-vfr";
  else if (name == "MVFR") cls = "cat-mvfr";
  else if (name == "IFR") cls = "cat-ifr";
  else if (name == "LIFR") cls = "cat-lifr";
  return "<span class=\"cat " + cls + "\">" + esc(name) + "</span>";
}

std::string groupsHtml(const std::vector<Group>& groups) {
  std::string out;
  for (const Group& g : groups) {
    out += "<span class=\"mt mt-" + esc(g.kind) + "\" tabindex=\"0\" data-tip=\"" + esc(g.tip) + "\">" + esc(g.text) + "</span> ";
  }
  if (!out.empty()) out.pop_back();
  return out;
}

}  // namespace wx

// ======================================================================== METAR

namespace {

using namespace wx;

struct Row {
  std::string label, value;
};

Json fail(const std::string& message) {
  Json out = Json::object();
  out.set("ok", false);
  out.set("error", message);
  return out;
}

bool isStation(const std::string& t) {
  if (t.size() != 4) return false;
  static const char* const reserved[] = {"AUTO", "CAVOK", "NOSIG", "TEMPO", "BECMG", "NSW", "COR", "NIL", "SPECI", "METAR"};
  for (const char* r : reserved) {
    if (t == r) return false;
  }
  return std::all_of(t.begin(), t.end(), [](unsigned char c) { return std::isalpha(c) != 0; });
}

std::string hhmm(int h, int m) { return two(h) + ":" + two(m); }

// Magnus-Tetens approximation (August-Roche-Magnus constants).
double relativeHumidity(double t, double td) {
  auto es = [](double c) { return std::exp(17.625 * c / (243.04 + c)); };
  return std::max(0.0, std::min(100.0, 100.0 * es(td) / es(t)));
}

std::string tempText(double c) { return fmt(c, 1) + " \xC2\xB0" + "C (" + fmt(c * 9.0 / 5.0 + 32.0, 1) + " \xC2\xB0" + "F)"; }

std::string timeText(int d, int h, int m) { return "Day " + std::to_string(d) + " at " + hhmm(h, m) + " UTC"; }

// ------------------------------------------------------------------- remarks

struct Remarks {
  std::vector<std::string> lines;
  bool slp = false;
  double slpHpa = 0;
  bool precise = false;
  double preciseT = 0, preciseD = 0;
};

double tenthsSigned(const std::string& s, std::size_t pos) {
  double v = intAt(s, pos + 1, 3) / 10.0;
  return s[pos] == '1' ? -v : v;
}

void parseRemark(const std::vector<std::string>& tk, std::size_t& i, std::vector<Group>& groups, Remarks& r) {
  const std::string& t = tk[i];
  auto add = [&](const std::string& kind, const std::string& tip, std::size_t used = 1) {
    std::string text = tk[i];
    for (std::size_t k = 1; k < used; ++k) text += " " + tk[i + k];
    groups.push_back({text, kind, tip});
    r.lines.push_back(tip);
    i += used;
  };
  if (t == "AO1") return add("rmk", "Automated station without a precipitation sensor (AO1)");
  if (t == "AO2") return add("rmk", "Automated station with a precipitation sensor (AO2)");
  if (t == "$") return add("rmk", "The automated station needs maintenance");
  if (t == "PRESRR") return add("rmk", "Pressure rising rapidly");
  if (t == "PRESFR") return add("rmk", "Pressure falling rapidly");
  if (t.rfind("SLP", 0) == 0 && t.size() == 6 && digitsAt(t, 3, 3)) {
    int n = intAt(t, 3, 3);
    r.slp = true;
    r.slpHpa = (n >= 500 ? 900.0 : 1000.0) + n / 10.0;
    return add("rmk", "Sea-level pressure " + fmt(r.slpHpa, 1) + " hPa");
  }
  if (t.size() == 9 && t[0] == 'T' && (t[1] == '0' || t[1] == '1') && digitsAt(t, 2, 3) && (t[5] == '0' || t[5] == '1') && digitsAt(t, 6, 3)) {
    r.precise = true;
    r.preciseT = tenthsSigned(t, 1);
    r.preciseD = tenthsSigned(t, 5);
    return add("rmk", "Precise temperature " + fmt(r.preciseT, 1) + " \xC2\xB0" + "C, dew point " + fmt(r.preciseD, 1) + " \xC2\xB0" + "C");
  }
  if (t.size() == 5 && (t[0] == '1' || t[0] == '2') && (t[1] == '0' || t[1] == '1') && digitsAt(t, 2, 3)) {
    return add("rmk", std::string(t[0] == '1' ? "6-hour maximum" : "6-hour minimum") + " temperature " + fmt(tenthsSigned(t, 1), 1) + " \xC2\xB0" + "C");
  }
  if (t.size() == 9 && t[0] == '4' && (t[1] == '0' || t[1] == '1') && digitsAt(t, 2, 3) && (t[5] == '0' || t[5] == '1') && digitsAt(t, 6, 3)) {
    return add("rmk", "24-hour maximum " + fmt(tenthsSigned(t, 1), 1) + " \xC2\xB0" + "C, minimum " + fmt(tenthsSigned(t, 5), 1) + " \xC2\xB0" + "C");
  }
  if (t.size() == 5 && t[0] == '5' && t[1] >= '0' && t[1] <= '8' && digitsAt(t, 2, 3)) {
    static const char* const trend[] = {"rising, then falling", "rising, then steady", "rising", "falling or steady, then rising",
                                        "steady", "falling, then rising", "falling, then steady", "falling", "steady or rising, then falling"};
    int a = t[1] - '0';
    double change = intAt(t, 2, 3) / 10.0;
    bool falling = a >= 5;
    return add("rmk", std::string("3-hour pressure tendency: ") + trend[a] + ", " + (falling ? "-" : "+") + fmt(change, 1) + " hPa");
  }
  if (t.size() == 5 && t[0] == 'P' && digitsAt(t, 1, 4)) return add("rmk", "Precipitation in the last hour: " + fmt(intAt(t, 1, 4) / 100.0, 2) + " in");
  if (t.size() == 5 && t[0] == '6' && digitsAt(t, 1, 4)) return add("rmk", "Precipitation in the last 3 or 6 hours: " + fmt(intAt(t, 1, 4) / 100.0, 2) + " in");
  if (t.size() == 5 && t[0] == '7' && digitsAt(t, 1, 4)) return add("rmk", "Precipitation in the last 24 hours: " + fmt(intAt(t, 1, 4) / 100.0, 2) + " in");
  if (t == "PK" && i + 2 < tk.size() && tk[i + 1] == "WND") {
    const std::string& v = tk[i + 2];
    std::size_t slash = v.find('/');
    if (slash != std::string::npos && slash >= 5 && digitsAt(v, 0, 3) && digitsAt(v, 3, slash - 3)) {
      std::string when = v.substr(slash + 1);
      std::string at = when.size() == 4 ? hhmm(intAt(when, 0, 2), intAt(when, 2, 2)) + " UTC" : "minute " + when;
      return add("rmk", "Peak wind from " + std::to_string(intAt(v, 0, 3)) + "\xC2\xB0 at " + std::to_string(intAt(v, 3, slash - 3)) + " kt, at " + at, 3);
    }
  }
  if (t == "WSHFT" && i + 1 < tk.size() && digitsAt(tk[i + 1], 0, 4)) {
    bool fropa = i + 2 < tk.size() && tk[i + 2] == "FROPA";
    return add("rmk", "Wind shift at " + hhmm(intAt(tk[i + 1], 0, 2), intAt(tk[i + 1], 2, 2)) + " UTC" + (fropa ? " (frontal passage)" : ""), fropa ? 3 : 2);
  }
  // Anything else is kept, shown as written.
  groups.push_back({t, "rmktoken", "Remark (shown as written)"});
  r.lines.push_back("Other remark: " + t);
  ++i;
}

std::string rowsHtml(const std::vector<Row>& rows) {
  std::string out = "<table class=\"mx-table\"><tbody>";
  for (const Row& r : rows) {
    out += "<tr><th scope=\"row\">" + esc(r.label) + "</th><td>";
    std::string line;
    bool first = true;
    for (std::size_t i = 0; i <= r.value.size(); ++i) {
      if (i == r.value.size() || r.value[i] == '\n') {
        out += (first ? "" : "<br>") + esc(line);
        first = false;
        line.clear();
      } else {
        line += r.value[i];
      }
    }
    out += "</td></tr>";
  }
  return out + "</tbody></table>";
}

}  // namespace

Json decodeMetar(const std::string& rawIn, const std::string& observedAt) {
  std::string norm = upperOf(rawIn);
  for (char& c : norm) {
    if (c == '=') c = ' ';
  }
  std::vector<std::string> tk = words(norm);
  if (tk.empty()) return fail("Paste a METAR first, for example: KABE 181451Z 00000KT 10SM CLR 09/M01 A3062");
  if (tk[0] == "TAF") return fail("That looks like a TAF (a forecast). Use the TAF decoder instead.");

  std::string type = "METAR";
  std::size_t i = 0;
  if (tk[i] == "METAR" || tk[i] == "SPECI") type = tk[i++];
  std::vector<Group> groups;
  if (type == "SPECI" || tk[0] == "METAR") groups.push_back({tk[i - 1], "type", type == "SPECI" ? "Special report, issued because conditions changed" : "Routine weather report"});
  bool corrected = false, automated = false, nil = false;
  if (i < tk.size() && tk[i] == "COR") {
    corrected = true;
    groups.push_back({"COR", "mod", "Corrected report"});
    ++i;
  }
  if (i >= tk.size() || !isStation(tk[i])) return fail("Could not find a 4-letter station code (for example KABE or SABE).");
  std::string station = tk[i];
  groups.push_back({station, "station", "Station (ICAO code): " + station});
  ++i;
  if (i >= tk.size() || tk[i].size() != 7 || !digitsAt(tk[i], 0, 6) || tk[i][6] != 'Z') {
    return fail("Could not find the observation time (for example 181451Z) after the station code.");
  }
  int day = intAt(tk[i], 0, 2), hour = intAt(tk[i], 2, 2), minute = intAt(tk[i], 4, 2);
  if (day < 1 || day > 31 || hour > 23 || minute > 59) return fail("The observation time " + tk[i] + " is not valid.");
  groups.push_back({tk[i], "time", "Observed on day " + std::to_string(day) + " at " + hhmm(hour, minute) + " UTC"});
  ++i;

  Elements body;
  bool hasTemp = false, hasDew = false;
  int temp = 0, dew = 0;
  bool hasPressure = false, pressureQ = false;
  double hpa = 0, inHg = 0;
  std::vector<std::string> warnings, trendRows;
  Remarks remarks;
  bool inRemarks = false, inTrend = false;
  Elements trendEl;
  std::string trendKind, trendTimes;

  auto flushTrend = [&]() {
    if (trendKind.empty()) return;
    trendRows.push_back((trendKind == "BECMG" ? "Becoming" : "Temporarily") + std::string(trendTimes.empty() ? "" : " (" + trendTimes + ")") + ": " + describe(trendEl));
    trendKind.clear();
    trendTimes.clear();
    trendEl = Elements{};
  };

  while (i < tk.size()) {
    const std::string& t = tk[i];
    if (t == "RMK") {
      flushTrend();
      groups.push_back({t, "rmk", "Remarks follow"});
      inRemarks = true;
      inTrend = false;
      ++i;
      continue;
    }
    if (inRemarks) {
      parseRemark(tk, i, groups, remarks);
      continue;
    }
    if (t == "NOSIG") {
      flushTrend();
      groups.push_back({t, "trend", "No significant change expected in the next 2 hours"});
      trendRows.push_back("No significant change expected in the next 2 hours");
      ++i;
      continue;
    }
    if (t == "BECMG" || t == "TEMPO") {
      flushTrend();
      trendKind = t;
      inTrend = true;
      groups.push_back({t, "trend", t == "BECMG" ? "Becoming: a lasting change is expected within 2 hours" : "Temporarily: a brief change is expected"});
      ++i;
      continue;
    }
    if (inTrend && t.size() == 6 && (t.rfind("FM", 0) == 0 || t.rfind("TL", 0) == 0 || t.rfind("AT", 0) == 0) && digitsAt(t, 2, 4)) {
      std::string word = t.rfind("FM", 0) == 0 ? "from " : t.rfind("TL", 0) == 0 ? "until " : "at ";
      std::string when = word + hhmm(intAt(t, 2, 2), intAt(t, 4, 2)) + " UTC";
      trendTimes += (trendTimes.empty() ? "" : ", ") + when;
      groups.push_back({t, "trend", cap(when)});
      ++i;
      continue;
    }
    if (!inTrend) {
      if (t == "COR") {  // ICAO places the correction flag before the station; some stations send it after the time
        corrected = true;
        groups.push_back({t, "mod", "Corrected report"});
        ++i;
        continue;
      }
      if (t == "AUTO") {
        automated = true;
        groups.push_back({t, "mod", "Fully automated report, no human observer"});
        ++i;
        continue;
      }
      if (t == "NIL") {
        nil = true;
        groups.push_back({t, "mod", "No report available"});
        ++i;
        continue;
      }
      std::size_t slash = t.find('/');
      bool tempForm = slash != std::string::npos && slash >= 2 && slash <= 3 && t.find_first_not_of("M0123456789/") == std::string::npos;
      if (tempForm && t.find('/') == t.rfind('/')) {
        std::string a = t.substr(0, slash), b = t.substr(slash + 1);
        auto val = [](const std::string& s) { return (s[0] == 'M' ? -1 : 1) * std::atoi(s.c_str() + (s[0] == 'M' ? 1 : 0)); };
        if (a.size() >= 2 && std::isdigit(static_cast<unsigned char>(a.back()))) {
          hasTemp = true;
          temp = val(a);
          if (!b.empty() && std::isdigit(static_cast<unsigned char>(b.back()))) hasDew = true, dew = val(b);
          groups.push_back({t, "temp", "Temperature " + std::to_string(temp) + " \xC2\xB0" + "C" + (hasDew ? ", dew point " + std::to_string(dew) + " \xC2\xB0" + "C" : ", dew point not reported")});
          ++i;
          continue;
        }
      }
      if (t.size() == 5 && (t[0] == 'Q' || t[0] == 'A') && digitsAt(t, 1, 4)) {
        hasPressure = true;
        pressureQ = t[0] == 'Q';
        int n = intAt(t, 1, 4);
        if (pressureQ) hpa = n, inHg = n * 0.0295300;
        else inHg = n / 100.0, hpa = inHg * 33.8639;
        groups.push_back({t, "qnh", pressureQ ? "QNH " + std::to_string(n) + " hPa (" + fmt(inHg, 2) + " inHg)" : "Altimeter " + fmt(inHg, 2) + " inHg (" + fmt(hpa, 1) + " hPa)"});
        ++i;
        continue;
      }
      if (t.rfind("RE", 0) == 0 && t.size() > 3) {
        std::string phrase;
        if (parseWx(t.substr(2), phrase)) {
          groups.push_back({t, "wx", "Recent weather (since the last report): " + phrase});
          ++i;
          continue;
        }
      }
      if (t.size() >= 8 && t[0] == 'R' && digitsAt(t, 1, 2) && t.find('/') != std::string::npos && t.size() - t.find('/') == 7 &&
          t.find_first_not_of("0123456789/", t.find('/') + 1) == std::string::npos) {
        groups.push_back({t, "rvr", "Runway state group (not decoded)"});
        ++i;
        continue;
      }
    }
    Elements& target = inTrend ? trendEl : body;
    Group g;
    std::size_t used = 1;
    if (parseElement(tk, i, target, g, used)) {
      groups.push_back(g);
      i += used;
      continue;
    }
    groups.push_back({t, "unknown", "Not recognized"});
    warnings.push_back("Could not decode '" + t + "'. It is shown as written.");
    ++i;
  }
  flushTrend();

  // ---------------------------------------------------------------- decoded table
  std::vector<Row> rows;
  std::string kind = type == "SPECI" ? "SPECI (special report)" : "METAR (routine report)";
  if (automated) kind += ", automated";
  if (corrected) kind += ", corrected";
  rows.push_back({"Report", kind});
  rows.push_back({"Station", station});
  std::string observed = timeText(day, hour, minute);
  if (!observedAt.empty()) observed = observedAt + " (" + observed + ")";
  rows.push_back({"Observed", observed});
  if (nil) rows.push_back({"Status", "No report available (NIL)"});
  if (body.wind.present) rows.push_back({"Wind (true direction)", windText(body.wind)});
  if (body.cavok) rows.push_back({"Visibility", "10 km or more (CAVOK)"});
  else if (body.vis.set) rows.push_back({"Visibility", visText(body.vis)});
  for (const std::string& x : body.extra) rows.push_back({"Runway / shear", x});
  if (!body.wx.empty()) {
    std::string v;
    for (std::size_t k = 0; k < body.wx.size(); ++k) v += (k ? "\n" : "") + body.wx[k];
    rows.push_back({"Weather", v});
  } else if (!body.cavok) {
    rows.push_back({"Weather", "None reported"});
  }
  if (body.cavok) {
    rows.push_back({"Clouds", "None below 5,000 ft, no cumulonimbus (CAVOK)"});
  } else if (!body.clouds.empty() || body.noCloud) {
    std::string v;
    for (const Cloud& c : body.clouds) v += (v.empty() ? "" : "\n") + cloudText(c);
    if (body.noCloud) v += (v.empty() ? "" : "\n") + body.noCloudText;
    rows.push_back({"Clouds", v});
  }
  int ceiling = ceilingFt(body);
  if (!body.cavok && (!body.clouds.empty() || body.noCloud)) {
    rows.push_back({"Ceiling", ceiling < 0 ? "None (no broken or overcast layer)" : thousands(ceiling) + " ft AGL"});
  }
  double humidity = -1, cloudBase = -1;
  if (hasTemp) rows.push_back({"Temperature", tempText(temp)});
  if (hasTemp && hasDew) {
    rows.push_back({"Dew point", tempText(dew)});
    humidity = relativeHumidity(temp, dew);
    rows.push_back({"Relative humidity (approx.)", "About " + fmt(humidity, 0) + "%, from temperature and dew point (Magnus formula)"});
    double spread = temp - dew;
    cloudBase = spread <= 0 ? 0 : spread * 400.0;
    rows.push_back({"Cloud base estimate (approx.)",
                    spread <= 0 ? "Air is saturated: fog or cloud at or near the surface"
                                : "About " + thousands(std::lround(cloudBase / 100.0) * 100) + " ft AGL for convective cloud (400 ft per \xC2\xB0" + "C of spread, a rule of thumb)"});
  }
  if (hasPressure) {
    rows.push_back({"Pressure", pressureQ ? "QNH " + fmt(hpa, 0) + " hPa = " + fmt(inHg, 2) + " inHg" : "Altimeter " + fmt(inHg, 2) + " inHg = " + fmt(hpa, 1) + " hPa"});
  }
  for (const std::string& t : trendRows) rows.push_back({"Trend", t});
  if (!remarks.lines.empty()) {
    std::string v;
    for (std::size_t k = 0; k < remarks.lines.size(); ++k) v += (k ? "\n" : "") + remarks.lines[k];
    rows.push_back({"Remarks", v});
  }
  Category cat = categoryOf(body);
  rows.push_back({"Flight category", cat.name + ": " + cat.why});

  std::string html = "<div class=\"mx\"><p class=\"mx-raw\" aria-label=\"The report, one colored group per element\">" + groupsHtml(groups) +
                     "</p><p class=\"mx-cat\">" + catBadge(cat.name) + " <span class=\"mx-why\">" + esc(cat.why) + "</span></p>" + rowsHtml(rows);
  if (!warnings.empty()) {
    html += "<ul class=\"mx-warn\">";
    for (const std::string& w : warnings) html += "<li>" + esc(w) + "</li>";
    html += "</ul>";
  }
  html += "</div>";

  Json groupsJson = Json::array();
  for (const Group& g : groups) {
    Json o = Json::object();
    o.set("text", g.text).set("kind", g.kind).set("tip", g.tip);
    groupsJson.push(std::move(o));
  }
  Json rowsJson = Json::array();
  for (const Row& r : rows) {
    Json o = Json::object();
    o.set("label", r.label).set("value", r.value);
    rowsJson.push(std::move(o));
  }
  Json warn = Json::array();
  for (const std::string& w : warnings) warn.push(w);

  Json wind = Json::object();
  wind.set("present", body.wind.present && !body.wind.missing);
  wind.set("calm", body.wind.calm).set("variable", body.wind.variable);
  wind.set("dir", body.wind.dir >= 0 ? Json(body.wind.dir) : Json());
  wind.set("speedKt", body.wind.present && !body.wind.missing ? Json(ktOf(body.wind)) : Json());
  wind.set("gustKt", body.wind.gust >= 0 ? Json(ktOf(body.wind, true)) : Json());

  Json out = Json::object();
  out.set("ok", true);
  out.set("station", station);
  out.set("category", cat.name);
  out.set("categoryWhy", cat.why);
  out.set("html", html);
  out.set("groups", std::move(groupsJson));
  out.set("rows", std::move(rowsJson));
  out.set("warnings", std::move(warn));
  out.set("wind", std::move(wind));
  out.set("tempC", hasTemp ? Json(temp) : Json());
  out.set("dewC", hasDew ? Json(dew) : Json());
  out.set("humidity", humidity >= 0 ? Json(std::round(humidity * 10) / 10) : Json());
  out.set("cloudBaseFt", cloudBase >= 0 ? Json(std::round(cloudBase)) : Json());
  out.set("hpa", hasPressure ? Json(std::round(hpa * 10) / 10) : Json());
  out.set("inHg", hasPressure ? Json(std::round(inHg * 100) / 100) : Json());
  out.set("ceilingFt", ceiling >= 0 ? Json(ceiling) : Json());
  out.set("slpHpa", remarks.slp ? Json(remarks.slpHpa) : Json());
  out.set("preciseTempC", remarks.precise ? Json(remarks.preciseT) : Json());
  out.set("preciseDewC", remarks.precise ? Json(remarks.preciseD) : Json());
  return out;
}

}  // namespace fc
