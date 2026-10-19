// Candlestick chart: validation of the history feed, a seeded illustrative
// series, and SVG + table rendering. All markup is produced here from numbers
// and escaped text, so callers may assign it to innerHTML.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "fc/json.hpp"

namespace fc {

// One trading day, portfolio and benchmarks indexed to 100 at the start.
struct Candle {
  std::string label;  // "2026-10-13" for live data, "Day 3" for illustrative
  double o = 0, h = 0, l = 0, c = 0;
  double spy = 0;
  double mix = 0;  // 60/40 SPY/AGG
};

struct ChartOptions {
  int width = 720;
  int height = 360;
  bool compact = false;       // candles only: no axes, labels or badge (decorative previews)
  bool illustrative = false;  // adds a visible "ILLUSTRATIVE DATA" badge
  int excessHeight = 0;       // > 0 adds the strip under the chart: bot close minus SPY close, per day
  std::string idPrefix = "fcz";  // unique per chart on a page (used for aria ids)
  std::string title = "Portfolio vs benchmarks, indexed to 100";
};

// Deterministic pseudo-random series (same seed, same output).
std::vector<Candle> illustrativeSeries(std::uint32_t seed, int days);

// Validates the /stats/history payload: [{date,o,h,l,c,spy,mix_60_40}].
// Only those seven keys are read, whatever else the feed contains. Missing
// o/h/l (close-only feeds) become flat candles. Needs at least 2 days.
bool parseHistory(const Json& feed, std::vector<Candle>& out, std::string& error);

// Returns {svg, table, points, plot, summary, n}, plus {excess, exPlot} when excessHeight > 0.
// The y axis always has exactly 5 ticks and the x axis day 1, the middle day and the last day.
// Each point carries ex (bot close minus SPY close, index points), chg (open to close, percent)
// and readout, a sentence about that day that starts with "Illustrative." while the data is.
Json renderChart(const std::vector<Candle>& series, const ChartOptions& options);

}  // namespace fc
