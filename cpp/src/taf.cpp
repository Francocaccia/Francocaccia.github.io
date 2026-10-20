#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

#include "clamp.hpp"
#include "fc/weather.hpp"
#include "weather_internal.hpp"

namespace fc {

namespace {

using namespace wx;

Json fail(const std::string& message) {
  Json out = Json::object();
  out.set("ok", false);
  out.set("error", message);
  return out;
}

struct Period {
  std::string kind;  // base, from, becmg, tempo, prob
  int prob = 0;
  bool alsoTempo = false;
  bool main = false;  // base and FM periods replace one another; the rest are changes
  double start = 0, end = 0;  // hours after the start of validity
  std::vector<Group> groups;
  Elements el;
  Category cat;
};

std::string cls(const std::string& cat) {
  if (cat == "VFR") return "cat-vfr";
  if (cat == "MVFR") return "cat-mvfr";
  if (cat == "IFR") return "cat-ifr";
  if (cat == "LIFR") return "cat-lifr";
  return "cat-unknown";
}

std::string hhmm(int h, int m) { return two(h) + ":" + two(m); }

struct Clock {
  int startDay, startHour, monthDays;
  // Day and hour (hour 0-23) for an offset in hours after the start of validity.
  void at(double off, int& day, int& hour, int& minute) const {
    long total = static_cast<long>(std::lround(off * 60.0)) + startHour * 60L;
    long dayAdd = total / (24 * 60);
    long rem = total % (24 * 60);
    day = static_cast<int>((startDay - 1 + dayAdd) % monthDays) + 1;
    hour = static_cast<int>(rem / 60);
    minute = static_cast<int>(rem % 60);
  }
  double offset(int d, int h, int m) const {
    int dd = d - startDay;
    if (dd < 0) dd += monthDays;
    return dd * 24.0 + h + m / 60.0 - startHour;
  }
  std::string stamp(double off) const {
    int d, h, m;
    at(off, d, h, m);
    return "day " + std::to_string(d) + " " + hhmm(h, m);
  }
};

bool isRange(const std::string& t) { return t.size() == 9 && digitsAt(t, 0, 4) && t[4] == '/' && digitsAt(t, 5, 4); }

}  // namespace

Json decodeTaf(const Json& in) {
  std::string norm = upperOf(in.at("raw").asString());
  for (char& c : norm) {
    if (c == '=') c = ' ';
  }
  std::vector<std::string> tk = words(norm);
  if (tk.empty()) return fail("Paste a TAF first.");
  std::size_t i = 0;
  if (tk[i] == "TAF") ++i;
  else if (tk[0] == "METAR" || tk[0] == "SPECI") return fail("That looks like a METAR (an observation). Use the METAR decoder instead.");
  std::string amend;
  if (i < tk.size() && (tk[i] == "AMD" || tk[i] == "COR")) amend = tk[i++];
  if (i >= tk.size() || tk[i].size() != 4 || !std::all_of(tk[i].begin(), tk[i].end(), [](unsigned char c) { return std::isalpha(c) != 0; })) {
    return fail("Could not find a 4-letter station code after TAF.");
  }
  std::string station = tk[i++];
  if (i >= tk.size() || tk[i].size() != 7 || !digitsAt(tk[i], 0, 6) || tk[i][6] != 'Z') return fail("Could not find the issue time (for example 161720Z).");
  int issueDay = intAt(tk[i], 0, 2), issueHour = intAt(tk[i], 2, 2), issueMin = intAt(tk[i], 4, 2);
  ++i;
  if (i < tk.size() && tk[i] == "NIL") return fail("This TAF is NIL: no forecast is available for the station.");
  if (i >= tk.size() || !isRange(tk[i])) return fail("Could not find the validity period (for example 1618/1724).");
  int monthDays = static_cast<int>(clampNumber(in.at("monthDays").asNumber(30), 28, 31, 30));
  Clock clock{intAt(tk[i], 0, 2), intAt(tk[i], 2, 2), monthDays};
  double validEnd = clock.offset(intAt(tk[i], 5, 2), intAt(tk[i], 7, 2), 0);
  if (validEnd <= 0) return fail("The validity period is not valid.");
  std::vector<Group> header = {{"TAF", "type", "Terminal aerodrome forecast"}};
  if (!amend.empty()) header.push_back({amend, "mod", amend == "AMD" ? "Amended forecast" : "Corrected forecast"});
  header.push_back({station, "station", "Station (ICAO code): " + station});
  header.push_back({tk[i - 1], "time", "Issued on day " + std::to_string(issueDay) + " at " + hhmm(issueHour, issueMin) + " UTC"});
  header.push_back({tk[i], "time", "Valid from " + clock.stamp(0) + " UTC to " + clock.stamp(validEnd) + " UTC"});
  ++i;

  std::vector<Period> periods(1);
  periods[0].kind = "base";
  periods[0].main = true;
  std::vector<std::string> warnings, temps;

  while (i < tk.size()) {
    const std::string& t = tk[i];
    Period* cur = &periods.back();
    auto begin = [&](const std::string& kind, bool main) {
      periods.emplace_back();
      periods.back().kind = kind;
      periods.back().main = main;
      return &periods.back();
    };
    if (t.size() == 8 && t.rfind("FM", 0) == 0 && digitsAt(t, 2, 6)) {
      Period* p = begin("from", true);
      p->start = clock.offset(intAt(t, 2, 2), intAt(t, 4, 2), intAt(t, 6, 2));
      p->groups.push_back({t, "trend", "From " + clock.stamp(p->start) + " UTC, conditions change to:"});
      ++i;
      continue;
    }
    if (t == "BECMG" || t == "TEMPO" || t.rfind("PROB", 0) == 0) {
      bool prob = t.rfind("PROB", 0) == 0;
      if (prob && !(t == "PROB30" || t == "PROB40")) {
        warnings.push_back("Could not decode '" + t + "'.");
        ++i;
        continue;
      }
      Period* p = begin(prob ? "prob" : t == "BECMG" ? "becmg" : "tempo", false);
      if (prob) p->prob = intAt(t, 4, 2);
      p->groups.push_back({t, "trend", prob ? t.substr(4) + "% probability of:" : t == "BECMG" ? "Becoming (a gradual, lasting change):" : "Temporarily (brief fluctuations):"});
      ++i;
      if (prob && i < tk.size() && tk[i] == "TEMPO") {
        p->alsoTempo = true;
        p->groups.push_back({"TEMPO", "trend", "Temporarily (brief fluctuations)"});
        ++i;
      }
      if (i < tk.size() && isRange(tk[i])) {
        p->start = clock.offset(intAt(tk[i], 0, 2), intAt(tk[i], 2, 2), 0);
        p->end = clock.offset(intAt(tk[i], 5, 2), intAt(tk[i], 7, 2), 0);
        p->groups.push_back({tk[i], "time", "From " + clock.stamp(p->start) + " UTC to " + clock.stamp(p->end) + " UTC"});
        ++i;
      } else {
        p->start = 0;
        p->end = validEnd;
        warnings.push_back("A " + t + " group has no time range, so it is shown across the whole forecast.");
      }
      continue;
    }
    // Forecast maximum and minimum temperature: TX22/1718Z, TN08/1710Z
    if ((t.rfind("TX", 0) == 0 || t.rfind("TN", 0) == 0) && t.size() >= 10 && t.back() == 'Z' && t.find('/') != std::string::npos) {
      std::size_t slash = t.find('/');
      std::string a = t.substr(2, slash - 2), b = t.substr(slash + 1, t.size() - slash - 2);
      if (b.size() == 4 && digitsAt(b, 0, 4)) {
        int v = (a[0] == 'M' ? -1 : 1) * std::atoi(a.c_str() + (a[0] == 'M' ? 1 : 0));
        std::string text = std::string(t[1] == 'X' ? "Forecast maximum " : "Forecast minimum ") + std::to_string(v) + " \xC2\xB0" +
                           "C around " + hhmm(intAt(b, 2, 2), 0) + " UTC on day " + std::to_string(intAt(b, 0, 2));
        temps.push_back(text);
        cur->groups.push_back({t, "temp", text});
        ++i;
        continue;
      }
    }
    Group g;
    std::size_t used = 1;
    if (parseElement(tk, i, cur->el, g, used)) {
      cur->groups.push_back(g);
      i += used;
      continue;
    }
    if (t.rfind("QNH", 0) == 0) {
      cur->groups.push_back({t, "qnh", "Forecast altimeter setting"});
      ++i;
      continue;
    }
    cur->groups.push_back({t, "unknown", "Not recognized"});
    warnings.push_back("Could not decode '" + t + "'. It is shown as written.");
    ++i;
  }

  // Base and FM periods follow one another; each ends where the next begins.
  std::vector<std::size_t> mains;
  for (std::size_t k = 0; k < periods.size(); ++k) {
    if (periods[k].main) mains.push_back(k);
  }
  for (std::size_t m = 0; m < mains.size(); ++m) {
    periods[mains[m]].end = m + 1 < mains.size() ? periods[mains[m + 1]].start : validEnd;
  }
  auto prevailing = [&](double at) -> const Period& {
    const Period* best = &periods[mains[0]];
    for (std::size_t m : mains) {
      if (periods[m].start <= at + 1e-9) best = &periods[m];
    }
    return *best;
  };
  std::vector<Elements> merged(periods.size());
  for (std::size_t k = 0; k < periods.size(); ++k) {
    Period& p = periods[k];
    Elements e = p.el;
    if (!p.main) {
      e = prevailing(p.start).el;
      if (p.el.cavok) {
        e.cavok = true;
      } else {
        if (p.el.vis.set) e.vis = p.el.vis, e.cavok = false;
        if (!p.el.clouds.empty() || p.el.noCloud) e.clouds = p.el.clouds, e.noCloud = p.el.noCloud, e.cavok = false;
        if (!p.el.wx.empty()) e.wx = p.el.wx;
        if (p.el.nsw) e.wx.clear();
      }
    }
    p.cat = categoryOf(e);
    merged[k] = e;
  }

  // ------------------------------------------------------------------- text list
  auto when = [&](const Period& p) {
    if (p.kind == "base") return "Initial conditions, " + clock.stamp(p.start) + " to " + clock.stamp(p.end) + " UTC";
    if (p.kind == "from") return "From " + clock.stamp(p.start) + " UTC (until " + clock.stamp(p.end) + ")";
    std::string label = p.kind == "becmg" ? "Becoming" : p.kind == "tempo" ? "Temporarily" : std::to_string(p.prob) + "% chance" + (p.alsoTempo ? ", temporarily" : "");
    return label + ", " + clock.stamp(p.start) + " to " + clock.stamp(p.end) + " UTC";
  };

  std::string html = "<div class=\"tf\"><p class=\"mx-raw\">" + groupsHtml(header) + "</p><ol class=\"tf-list\">";
  Json periodsJson = Json::array();
  for (std::size_t k = 0; k < periods.size(); ++k) {
    const Period& p = periods[k];
    std::string text = describe(p.el);
    html += "<li class=\"tf-p tf-" + p.kind + "\"><h3><span class=\"tf-when\">" + esc(when(p)) + "</span> " + catBadge(p.cat.name) +
            "</h3><p class=\"mx-raw\">" + groupsHtml(p.groups) + "</p><p class=\"tf-text\">" + esc(text) + "</p>";
    if (!p.main) html += "<p class=\"tf-note\">Category here combines this change with the prevailing conditions: " + esc(p.cat.why) + "</p>";
    else html += "<p class=\"tf-note\">" + esc(p.cat.why) + "</p>";
    html += "</li>";
    Json o = Json::object();
    o.set("kind", p.kind).set("prob", p.prob).set("start", p.start).set("end", p.end);
    o.set("category", p.cat.name).set("text", text).set("when", when(p));
    periodsJson.push(std::move(o));
  }
  html += "</ol>";
  for (const std::string& t : temps) html += "<p class=\"tf-note\">" + esc(t) + ".</p>";
  if (!warnings.empty()) {
    html += "<ul class=\"mx-warn\">";
    for (const std::string& w : warnings) html += "<li>" + esc(w) + "</li>";
    html += "</ul>";
  }
  html += "</div>";

  // -------------------------------------------------------------------- timeline
  int W = static_cast<int>(clampNumber(in.at("width").asNumber(720), 240, 1200, 720));
  bool narrow = W < 500;
  std::string prefix = "fct";
  if (in.at("idPrefix").isString()) {
    std::string id;
    for (char c : in.at("idPrefix").asString()) {
      if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') id += c;
    }
    if (!id.empty()) prefix = id;
  }
  const double ml = 6, mr = 6, laneH = 26, gap = 4;
  std::size_t changeCount = 0;
  for (const Period& p : periods) changeCount += p.main ? 0 : 1;
  double top = 6;
  double mainY = top;
  double height = top + laneH + (changeCount ? gap : 0) + changeCount * (laneH + gap) + 30;
  auto X = [&](double h) { return ml + h / validEnd * (W - ml - mr); };
  auto num = [](double v) { return fmt(v, 1); };

  std::string svg = "<svg class=\"tl-svg\" xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " + std::to_string(W) + " " + std::to_string(static_cast<int>(height)) +
                    "\" role=\"img\" aria-labelledby=\"" + prefix + "-t " + prefix + "-d\"><title id=\"" + prefix + "-t\">Forecast timeline for " + esc(station) +
                    "</title><desc id=\"" + prefix + "-d\">" + std::to_string(periods.size()) + " forecast periods over " + fmt(validEnd, 0) +
                    " hours, colored by flight category. The list below gives every period in words.</desc>";
  // Grid and axis labels
  double step = validEnd <= 18 ? 3 : narrow ? 12 : 6;
  double axisY = height - 12;
  for (double h = 0; h <= validEnd + 1e-9; h += step) {
    svg += "<line class=\"tl-grid\" x1=\"" + num(X(h)) + "\" x2=\"" + num(X(h)) + "\" y1=\"" + num(top) + "\" y2=\"" + num(axisY - 12) + "\"/>";
    int d, hr, mn;
    clock.at(h, d, hr, mn);
    std::string label = two(hr) + "Z";
    if (h == 0 || hr == 0) label = "d" + std::to_string(d) + " " + label;
    std::string anchor = h == 0 ? "start" : (h + step > validEnd + 1e-9 ? "end" : "middle");
    svg += "<text class=\"tl-tick\" x=\"" + num(X(h)) + "\" y=\"" + num(axisY) + "\" text-anchor=\"" + anchor + "\">" + esc(label) + "</text>";
  }
  auto bar = [&](const Period& p, double y, const std::string& label, bool labelOutside) {
    double x0 = X(std::max(0.0, p.start)), x1 = X(std::min(validEnd, p.end));
    std::string out = "<rect class=\"tl-bar " + cls(p.cat.name) + " tl-" + p.kind + "\" x=\"" + num(x0) + "\" y=\"" + num(y) + "\" width=\"" +
                      num(std::max(2.0, x1 - x0)) + "\" height=\"" + num(laneH) + "\" rx=\"2\"/>";
    double textW = 7.0 * static_cast<double>(label.size()) + 8;
    if (x1 - x0 > textW) {
      out += "<text class=\"tl-label\" x=\"" + num(x0 + 6) + "\" y=\"" + num(y + laneH / 2 + 4) + "\">" + esc(label) + "</text>";
    } else if (labelOutside && x1 + textW < W - mr) {
      out += "<text class=\"tl-label-out\" x=\"" + num(x1 + 6) + "\" y=\"" + num(y + laneH / 2 + 4) + "\">" + esc(label) + "</text>";
    } else if (labelOutside && x0 - textW > ml) {
      out += "<text class=\"tl-label-out\" x=\"" + num(x0 - 6) + "\" y=\"" + num(y + laneH / 2 + 4) + "\" text-anchor=\"end\">" + esc(label) + "</text>";
    }
    return out;
  };
  for (const Period& p : periods) {
    if (!p.main) continue;
    std::string label = p.kind == "base" ? "BASE " + p.cat.name : "FM " + [&] {
      int d, h, m;
      clock.at(p.start, d, h, m);
      return hhmm(h, m);
    }() + " " + p.cat.name;
    svg += bar(p, mainY, label, false);
  }
  double y = mainY + laneH + gap;
  for (const Period& p : periods) {
    if (p.main) continue;
    std::string label = p.kind == "becmg" ? "BECMG" : p.kind == "tempo" ? "TEMPO" : "PROB" + std::to_string(p.prob) + (p.alsoTempo ? " TEMPO" : "");
    svg += bar(p, y + 0, label + " " + p.cat.name, true);
    y += laneH + gap;
  }
  svg += "</svg>";

  Json out = Json::object();
  out.set("ok", true);
  out.set("station", station);
  out.set("validityHours", validEnd);
  out.set("periods", std::move(periodsJson));
  out.set("html", html);
  out.set("timeline", svg);
  Json warn = Json::array();
  for (const std::string& w : warnings) warn.push(w);
  out.set("warnings", std::move(warn));
  return out;
}

// ========================================================================= wind

Json windComponents(const Json& in) {
  const double kPi = 3.14159265358979323846;
  auto rad = [&](double d) { return d * kPi / 180.0; };
  if (!in.at("dir").isNumber() || !in.at("speed").isNumber()) return fail("Enter the wind direction and speed.");
  double dir = in.at("dir").asNumber(), speed = in.at("speed").asNumber();
  double gust = in.at("gust").isNumber() ? in.at("gust").asNumber() : -1;
  if (dir < 0 || dir > 360) return fail("Wind direction must be between 0 and 360 degrees.");
  if (speed < 0 || speed > 200 || gust > 300) return fail("Wind speed must be between 0 and 200 knots.");
  if (gust >= 0 && gust < speed) return fail("The gust must be at least as strong as the steady wind.");
  std::string rwy = upperOf(in.at("runway").asString());
  std::string digits;
  char suffix = 0;
  for (char c : rwy) {
    if (std::isdigit(static_cast<unsigned char>(c))) digits += c;
    else if ((c == 'L' || c == 'C' || c == 'R') && suffix == 0) suffix = c;
    else if (c != ' ') return fail("Enter a runway number from 01 to 36, for example 27 or 09L.");
  }
  int number = digits.empty() || digits.size() > 2 ? -1 : std::atoi(digits.c_str());
  if (number < 1 || number > 36) return fail("Enter a runway number from 01 to 36, for example 27 or 09L.");
  double variation = in.at("variation").isNumber() ? in.at("variation").asNumber() : 0;
  if (std::fabs(variation) > 90) return fail("Magnetic variation must be between -90 and 90 degrees.");
  int heading = number * 10;

  // METAR wind is true; runway headings are magnetic. East variation is positive: magnetic = true - variation.
  double windMag = std::fmod(dir - variation + 720.0, 360.0);
  auto comp = [&](double s, double& head, double& cross) {
    double a = rad(windMag - heading);
    head = s * std::cos(a);
    cross = s * std::sin(a);  // positive: wind from the right of the runway heading
  };
  double head, cross, headG = 0, crossG = 0;
  comp(speed, head, cross);
  if (gust >= 0) comp(gust, headG, crossG);

  auto headText = [](double h) {
    if (std::fabs(h) < 0.5) return std::string("No headwind or tailwind");
    return h >= 0 ? "Headwind " + fmt(std::fabs(h), 0) + " kt" : "Tailwind " + fmt(std::fabs(h), 0) + " kt";
  };
  auto crossText = [](double c) {
    if (std::fabs(c) < 0.5) return std::string("No crosswind");
    return "Crosswind " + fmt(std::fabs(c), 0) + " kt from the " + (c > 0 ? "right" : "left");
  };

  // Diagram: runway pointing up, wind arriving from its relative direction.
  std::string prefix = "fcw";
  if (in.at("idPrefix").isString()) {
    std::string id;
    for (char c : in.at("idPrefix").asString()) {
      if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') id += c;
    }
    if (!id.empty()) prefix = id;
  }
  const double S = 240, cx = 120, cy = 124;
  double rel = rad(windMag - heading);
  auto num = [](double v) { return fmt(v, 1); };
  double sc = speed > 0 ? 62.0 / std::max(speed, 1.0) : 0;
  std::string label = rwy.empty() ? "" : rwy;
  std::string svg = "<svg class=\"xw-svg\" xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " + num(S) + " " + num(S) + "\" role=\"img\" aria-labelledby=\"" + prefix +
                    "-t\"><title id=\"" + prefix + "-t\">" + esc(headText(head) + ", " + crossText(cross)) + " on runway " + esc(label) + "</title>";
  svg += "<circle class=\"xw-ring\" cx=\"" + num(cx) + "\" cy=\"" + num(cy) + "\" r=\"96\"/>";
  svg += "<rect class=\"xw-rwy\" x=\"" + num(cx - 11) + "\" y=\"" + num(cy - 70) + "\" width=\"22\" height=\"140\" rx=\"2\"/>";
  svg += "<line class=\"xw-cl\" x1=\"" + num(cx) + "\" x2=\"" + num(cx) + "\" y1=\"" + num(cy - 62) + "\" y2=\"" + num(cy + 62) + "\"/>";
  svg += "<text class=\"xw-num\" x=\"" + num(cx) + "\" y=\"" + num(cy - 44) + "\" text-anchor=\"middle\">" + esc(std::to_string(number)) + "</text>";
  svg += "<text class=\"xw-note\" x=\"" + num(cx) + "\" y=\"14\" text-anchor=\"middle\">runway heading up</text>";
  if (speed > 0) {
    // Arrow from the direction the wind comes from toward the center.
    double ax = cx + 100 * std::sin(rel), ay = cy - 100 * std::cos(rel);
    double bx = cx + 30 * std::sin(rel), by = cy - 30 * std::cos(rel);
    double ang = std::atan2(by - ay, bx - ax) * 180.0 / kPi;
    svg += "<line class=\"xw-wind\" x1=\"" + num(ax) + "\" y1=\"" + num(ay) + "\" x2=\"" + num(bx) + "\" y2=\"" + num(by) + "\"/>";
    svg += "<path class=\"xw-wind-head\" d=\"M-9 -6 L3 0 L-9 6 Z\" transform=\"translate(" + num(bx) + " " + num(by) + ") rotate(" + num(ang) + ")\"/>";
    // Components: along the runway (headwind or tailwind) and across it.
    double hx = cx + 40, hy = cy, hlen = head * sc;
    svg += "<line class=\"xw-head\" x1=\"" + num(hx) + "\" y1=\"" + num(hy + hlen) + "\" x2=\"" + num(hx) + "\" y2=\"" + num(hy) + "\"/>";
    double cxs = cx, cys = cy + 78, clen = -cross * sc;
    svg += "<line class=\"xw-cross\" x1=\"" + num(cxs - clen) + "\" y1=\"" + num(cys) + "\" x2=\"" + num(cxs) + "\" y2=\"" + num(cys) + "\"/>";
  }
  svg += "</svg>";

  Json out = Json::object();
  out.set("headwind", std::round(head * 10) / 10);
  out.set("crosswind", std::round(cross * 10) / 10);
  out.set("headText", headText(head));
  out.set("crossText", crossText(cross));
  out.set("runwayHeading", heading);
  out.set("windMagnetic", std::round(windMag));
  if (gust >= 0) {
    out.set("gustHeadText", headText(headG));
    out.set("gustCrossText", crossText(crossG));
    out.set("gustHeadwind", std::round(headG * 10) / 10).set("gustCrosswind", std::round(crossG * 10) / 10);
  }
  out.set("svg", svg);
  return out;
}

}  // namespace fc
