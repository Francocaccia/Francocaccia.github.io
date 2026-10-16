#include "fc/chart.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fc {

namespace {

constexpr std::size_t kMaxDays = 400;

std::string num(double v, int decimals = 2) {
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
  return buf;
}

double round2(double v) { return std::round(v * 100.0) / 100.0; }

std::string escape(const std::string& s) {
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

// mulberry32: tiny, fast, good enough for a decorative series.
class Rng {
 public:
  explicit Rng(std::uint32_t seed) : state_(seed) {}
  double uniform() {
    state_ += 0x6D2B79F5u;
    std::uint32_t t = state_;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + ((t ^ (t >> 7)) * (t | 61u));
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
  }
  double normal() {
    double u1 = std::max(uniform(), 1e-12);
    double u2 = uniform();
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
  }

 private:
  std::uint32_t state_;
};

bool isDate(const std::string& s) {
  if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (i == 4 || i == 7) continue;
    if (s[i] < '0' || s[i] > '9') return false;
  }
  int month = (s[5] - '0') * 10 + (s[6] - '0');
  int day = (s[8] - '0') * 10 + (s[9] - '0');
  return month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

bool plausibleIndex(double v) { return std::isfinite(v) && v >= 20.0 && v <= 500.0; }

// Rounds a span to a "nice" step: 1, 2 or 5 times a power of ten.
double niceStep(double span, int targetTicks) {
  double raw = span / std::max(1, targetTicks);
  double mag = std::pow(10.0, std::floor(std::log10(raw)));
  double norm = raw / mag;
  double nice = norm < 1.5 ? 1 : norm < 3.5 ? 2 : norm < 7.5 ? 5 : 10;
  return nice * mag;
}

}  // namespace

std::vector<Candle> illustrativeSeries(std::uint32_t seed, int days) {
  days = std::max(2, std::min(days, static_cast<int>(kMaxDays)));
  Rng rng(seed);
  std::vector<Candle> out;
  double bot = 100.0, spy = 100.0, mix = 100.0;
  for (int i = 0; i < days; ++i) {
    double market = rng.normal();
    double idio = rng.normal();
    Candle c;
    c.label = "Day " + std::to_string(i + 1);
    c.o = bot;
    double botMove = 0.05 + 0.5 * market + 0.8 * idio;  // percent
    c.c = bot * (1.0 + botMove / 100.0);
    c.h = std::max(c.o, c.c) * (1.0 + std::fabs(rng.normal()) * 0.25 / 100.0);
    c.l = std::min(c.o, c.c) * (1.0 - std::fabs(rng.normal()) * 0.25 / 100.0);
    spy *= 1.0 + (0.04 + 0.7 * market) / 100.0;
    mix *= 1.0 + (0.03 + 0.4 * market) / 100.0;
    bot = c.c;
    c.o = round2(c.o);
    c.c = round2(c.c);
    c.h = round2(std::max({c.h, c.o, c.c}));
    c.l = round2(std::min({c.l, c.o, c.c}));
    c.spy = round2(spy);
    c.mix = round2(mix);
    out.push_back(c);
  }
  out.front().o = 100.0;
  out.front().h = std::max(out.front().h, 100.0);
  out.front().l = std::min(out.front().l, 100.0);
  return out;
}

bool parseHistory(const Json& feed, std::vector<Candle>& out, std::string& error) {
  if (!feed.isArray()) {
    error = "history must be an array";
    return false;
  }
  std::vector<Candle> result;
  std::size_t index = 0;
  for (const Json& row : feed.items()) {
    std::string where = "row " + std::to_string(index++);
    if (!row.isObject()) {
      error = where + " is not an object";
      return false;
    }
    Candle c;
    if (!row.at("date").isString() || !isDate(row.at("date").asString())) {
      error = where + ": date must be YYYY-MM-DD";
      return false;
    }
    c.label = row.at("date").asString();
    if (!result.empty() && !(result.back().label < c.label)) {
      error = where + ": dates must be strictly increasing";
      return false;
    }
    if (!row.at("c").isNumber() || !row.at("spy").isNumber() || !row.at("mix_60_40").isNumber()) {
      error = where + ": c, spy and mix_60_40 must be numbers";
      return false;
    }
    c.c = row.at("c").asNumber();
    c.o = row.at("o").isNumber() ? row.at("o").asNumber() : c.c;
    c.h = row.at("h").isNumber() ? row.at("h").asNumber() : c.c;
    c.l = row.at("l").isNumber() ? row.at("l").asNumber() : c.c;
    c.spy = row.at("spy").asNumber();
    c.mix = row.at("mix_60_40").asNumber();
    for (double v : {c.o, c.h, c.l, c.c, c.spy, c.mix}) {
      // Indexed values sit near 100. Anything else (dollar amounts, raw prices) is rejected.
      if (!plausibleIndex(v)) {
        error = where + ": value outside the indexed range";
        return false;
      }
    }
    c.h = std::max({c.h, c.o, c.c});
    c.l = std::min({c.l, c.o, c.c});
    result.push_back(c);
  }
  if (result.size() < 2) {
    error = "need at least 2 trading days";
    return false;
  }
  if (result.size() > kMaxDays) result.erase(result.begin(), result.end() - static_cast<long>(kMaxDays));
  out = std::move(result);
  return true;
}

Json renderChart(const std::vector<Candle>& s, const ChartOptions& opt) {
  const int W = opt.width, H = opt.height;
  const bool compact = opt.compact;
  const bool narrow = W < 500;
  const double ml = compact ? 2 : 44;
  const double mr = compact ? 2 : (narrow ? 78 : 88);
  const double mt = compact ? 2 : 26;
  const double mb = compact ? 2 : 26;
  const double x0 = ml, x1 = W - mr, y0 = mt, y1 = H - mb;
  const std::size_t n = s.size();

  double lo = 100.0, hi = 100.0;
  for (const Candle& c : s) {
    lo = std::min({lo, c.l, c.spy, c.mix});
    hi = std::max({hi, c.h, c.spy, c.mix});
  }
  double pad = std::max((hi - lo) * 0.08, 0.5);
  lo -= pad;
  hi += pad;
  auto Y = [&](double v) { return y1 - (v - lo) / (hi - lo) * (y1 - y0); };
  const double slot = n ? (x1 - x0) / static_cast<double>(n) : 1.0;
  auto X = [&](std::size_t i) { return x0 + slot * (static_cast<double>(i) + 0.5); };
  const double bodyW = std::max(2.0, std::min(14.0, slot - 2.5));

  std::string svg;
  svg.reserve(8192);
  const std::string tid = opt.idPrefix + "-t", did = opt.idPrefix + "-d";
  std::string summary;
  if (n) {
    const Candle& last = s.back();
    summary = std::to_string(n) + " trading days, " + s.front().label + " to " + last.label +
              ". Last close: portfolio " + num(last.c) + ", SPY " + num(last.spy) + ", 60/40 " +
              num(last.mix) + ". Start is 100." + (opt.illustrative ? " Illustrative data, not real results." : "");
  }

  svg += "<svg class=\"fcz-svg\" xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " + std::to_string(W) + " " +
         std::to_string(H) + "\"";
  if (compact) {
    svg += " aria-hidden=\"true\" focusable=\"false\">";
  } else {
    svg += " role=\"img\" aria-labelledby=\"" + tid + " " + did + "\"><title id=\"" + tid + "\">" +
           escape(opt.title) + "</title><desc id=\"" + did + "\">" + escape(summary) + "</desc>";
  }

  if (!compact) {
    // Gridlines and y-axis ticks: hairlines, one step off the surface.
    double step = niceStep(hi - lo, 4);
    for (double t = std::ceil(lo / step) * step; t <= hi; t += step) {
      std::string y = num(Y(t), 1);
      svg += "<line class=\"fcz-grid\" x1=\"" + num(x0, 1) + "\" x2=\"" + num(x1, 1) + "\" y1=\"" + y + "\" y2=\"" + y +
             "\"/><text class=\"fcz-tick\" x=\"" + num(x0 - 8, 1) + "\" y=\"" + num(Y(t) + 4, 1) +
             "\" text-anchor=\"end\">" + num(t, step < 1 ? 1 : 0) + "</text>";
    }
    // The index base (100) is the one reference line drawn a little stronger.
    std::string by = num(Y(100.0), 1);
    svg += "<line class=\"fcz-base\" x1=\"" + num(x0, 1) + "\" x2=\"" + num(x1, 1) + "\" y1=\"" + by + "\" y2=\"" + by + "\"/>";
    // X labels: first and last always, middle when there is room.
    auto xlabel = [&](std::size_t i, const char* anchor) {
      svg += "<text class=\"fcz-tick\" x=\"" + num(i == 0 ? x0 : (i + 1 == n ? x1 : X(i)), 1) + "\" y=\"" +
             std::to_string(H - 8) + "\" text-anchor=\"" + anchor + "\">" + escape(s[i].label) + "</text>";
    };
    if (n >= 1) xlabel(0, "start");
    if (n >= 2) xlabel(n - 1, "end");
    if (n >= 5 && !narrow) xlabel(n / 2, "middle");
    if (opt.illustrative) {
      svg += "<text class=\"fcz-badge\" x=\"" + num(x0, 1) + "\" y=\"14\">ILLUSTRATIVE DATA</text>";
    }
  }

  // Candles: filled body when the day closed up, hollow when it closed down.
  for (std::size_t i = 0; i < n; ++i) {
    const Candle& c = s[i];
    bool up = c.c >= c.o;
    double top = Y(std::max(c.o, c.c)), bottom = Y(std::min(c.o, c.c));
    double height = std::max(1.5, bottom - top);
    const char* cls = up ? "up" : "dn";
    svg += std::string("<line class=\"fcz-wick ") + cls + "\" x1=\"" + num(X(i), 1) + "\" x2=\"" + num(X(i), 1) +
           "\" y1=\"" + num(Y(c.h), 1) + "\" y2=\"" + num(Y(c.l), 1) + "\"/><rect class=\"fcz-body " + cls + "\" x=\"" +
           num(X(i) - bodyW / 2, 1) + "\" y=\"" + num(top, 1) + "\" width=\"" + num(bodyW, 1) + "\" height=\"" +
           num(height, 1) + "\" rx=\"1\"/>";
  }

  Json points = Json::array();
  for (std::size_t i = 0; i < n; ++i) {
    Json p = Json::object();
    p.set("x", std::round(X(i) * 10) / 10);
    p.set("label", s[i].label);
    p.set("o", s[i].o).set("h", s[i].h).set("l", s[i].l).set("c", s[i].c);
    p.set("spy", s[i].spy).set("mix", s[i].mix);
    points.push(std::move(p));
  }

  if (!compact && n >= 2) {
    // Benchmark lines (2px) with end dots and, where they fit, direct labels.
    auto line = [&](const char* cls, double Candle::*field) {
      std::string pts;
      for (std::size_t i = 0; i < n; ++i) pts += num(X(i), 1) + "," + num(Y(s[i].*field), 1) + " ";
      svg += std::string("<polyline class=\"fcz-line ") + cls + "\" points=\"" + pts + "\"/>";
    };
    line("spy", &Candle::spy);
    line("mix", &Candle::mix);

    struct End { const char* cls; const char* name; double v; double y; bool keep; };
    const Candle& last = s.back();
    std::vector<End> ends = {{"bot", "Bot", last.c, Y(last.c), true},
                             {"spy", "SPY", last.spy, Y(last.spy), true},
                             {"mix", "60/40", last.mix, Y(last.mix), true}};
    // Priority order is the list order. A label too close to a kept one is dropped
    // (the legend and tooltip still carry it) rather than nudged away from its line.
    for (std::size_t i = 1; i < ends.size(); ++i) {
      for (std::size_t j = 0; j < i; ++j) {
        if (ends[j].keep && std::fabs(ends[j].y - ends[i].y) < 13) ends[i].keep = false;
      }
    }
    for (const End& e : ends) {
      double cx = X(n - 1);
      if (std::string(e.cls) != "bot") {
        svg += std::string("<circle class=\"fcz-dot ") + e.cls + "\" cx=\"" + num(cx, 1) + "\" cy=\"" + num(e.y, 1) + "\" r=\"4\"/>";
      }
      if (e.keep) {
        svg += "<text class=\"fcz-end\" x=\"" + num(x1 + 8, 1) + "\" y=\"" + num(e.y + 4, 1) + "\">" + escape(e.name) + " " +
               num(e.v, 1) + "</text>";
      }
    }
  }
  svg += "</svg>";

  // Table twin: every value the chart shows, reachable without hovering.
  std::string table = "<table class=\"fcz-table\"><caption>" +
                      std::string(opt.illustrative ? "Illustrative series, not real results. " : "") +
                      "Daily values indexed to 100 at the start. Percentages only.</caption><thead><tr>"
                      "<th scope=\"col\">Day</th><th scope=\"col\">Open</th><th scope=\"col\">High</th>"
                      "<th scope=\"col\">Low</th><th scope=\"col\">Close</th><th scope=\"col\">SPY</th>"
                      "<th scope=\"col\">60/40</th></tr></thead><tbody>";
  for (const Candle& c : s) {
    table += "<tr><th scope=\"row\">" + escape(c.label) + "</th><td>" + num(c.o) + "</td><td>" + num(c.h) + "</td><td>" +
             num(c.l) + "</td><td>" + num(c.c) + "</td><td>" + num(c.spy) + "</td><td>" + num(c.mix) + "</td></tr>";
  }
  table += "</tbody></table>";

  Json plot = Json::object();
  plot.set("x0", x0).set("x1", x1).set("y0", y0).set("y1", y1).set("w", W).set("h", H);

  Json out = Json::object();
  out.set("svg", svg);
  out.set("table", table);
  out.set("points", std::move(points));
  out.set("plot", std::move(plot));
  out.set("summary", summary);
  out.set("n", static_cast<double>(n));
  return out;
}

}  // namespace fc
