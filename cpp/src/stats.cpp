#include "fc/stats.hpp"

#include <cmath>
#include <cstdio>

namespace fc {

namespace {

const char* const kMinus = "\xE2\x88\x92";  // U+2212, reads as "minus" in screen readers

bool isDate(const std::string& s) {
  if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (i != 4 && i != 7 && (s[i] < '0' || s[i] > '9')) return false;
  }
  return true;
}

// "2026-10-16T22:23:56Z" -> "2026-10-16 22:23 UTC"; anything else -> "".
std::string asOf(const std::string& s) {
  if (s.size() != 20 || s[10] != 'T' || s[19] != 'Z' || !isDate(s.substr(0, 10)) || s[13] != ':' || s[16] != ':') return "";
  for (int i : {11, 12, 14, 15, 17, 18}) {
    if (s[static_cast<std::size_t>(i)] < '0' || s[static_cast<std::size_t>(i)] > '9') return "";
  }
  return s.substr(0, 10) + " " + s.substr(11, 5) + " UTC";
}

std::string percent(double v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.2f", std::fabs(v));
  std::string digits = buf;
  if (digits == "0.00") return "0.00%";
  return (v > 0 ? std::string("+") : std::string(kMinus)) + digits + "%";
}

struct TileSpec {
  const char* id;
  const char* label;
  const char* field;
  bool signedTone;  // drawdown is always negative by definition, so it is not toned
};

}  // namespace

Json normalizeStats(const Json& raw, const std::string& trackingSince) {
  static const TileSpec specs[] = {
      {"return", "Return", "bot_return_pct", true},
      {"vs_spy", "vs SPY", "excess_return_pct", true},
      {"vs_6040", "vs 60/40", "bot_vs_60_40_pct", true},
      {"max_drawdown", "Max drawdown", "max_drawdown_pct", false},
  };

  std::string start = isDate(raw.at("start_date").asString()) ? raw.at("start_date").asString() : trackingSince;
  const Json& daysField = raw.at("days_tracked");
  bool daysKnown = daysField.isNumber() && std::isfinite(daysField.asNumber()) && daysField.asNumber() >= 0 &&
                   daysField.asNumber() < 100000;
  int days = daysKnown ? static_cast<int>(daysField.asNumber()) : 0;

  Json tiles = Json::array();
  bool hasData = false;
  for (const TileSpec& spec : specs) {
    const Json& v = raw.at(spec.field);
    bool valid = v.isNumber() && std::isfinite(v.asNumber()) && std::fabs(v.asNumber()) <= 1000.0;
    Json tile = Json::object();
    tile.set("id", spec.id).set("label", spec.label);
    if (valid) {
      hasData = true;
      double value = v.asNumber();
      tile.set("value", value);
      std::string text = percent(value);
      tile.set("text", text);
      // A value that prints as 0.00% is not a gain or a loss, whatever sign it carries underneath.
      const char* tone = !spec.signedTone || text == "0.00%" ? "neutral" : (value > 0 ? "pos" : "neg");
      tile.set("tone", tone);
      std::string caption = "since " + start;
      if (daysKnown) caption += ", " + std::to_string(days) + (days == 1 ? " trading day" : " trading days");
      tile.set("caption", caption);
    } else {
      tile.set("value", Json());
      tile.set("text", "\xE2\x80\x94");  // em dash
      tile.set("tone", "none");
      tile.set("caption", "tracking since " + start);
    }
    tiles.push(std::move(tile));
  }

  std::string note;
  if (hasData && daysKnown && days < 20) {
    note = "Only " + std::to_string(days) + (days == 1 ? " trading day" : " trading days") +
           " so far. Too early to read anything into these numbers.";
  }

  Json out = Json::object();
  out.set("tiles", std::move(tiles));
  out.set("daysTracked", daysKnown ? Json(days) : Json());
  out.set("startDate", start);
  out.set("asOf", asOf(raw.at("last_updated").asString()));
  out.set("note", note);
  out.set("hasData", hasData);
  return out;
}

}  // namespace fc
