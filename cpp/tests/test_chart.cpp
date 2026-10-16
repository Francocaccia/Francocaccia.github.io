#include <algorithm>

#include "fc/api.hpp"
#include "fc/chart.hpp"
#include "fc/json.hpp"
#include "test.hpp"

using fc::Candle;
using fc::Json;

namespace {

Json parse(const std::string& text) {
  Json out;
  std::string error;
  bool ok = Json::parse(text, out, error);
  CHECK(ok);
  return out;
}

bool contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

const char* kFeed = R"([
  {"date":"2026-10-13","o":100,"h":100.4,"l":99.5,"c":99.8,"spy":99.6,"mix_60_40":99.9},
  {"date":"2026-10-14","o":99.8,"h":100.3,"l":98.9,"c":100.0,"spy":99.3,"mix_60_40":99.7},
  {"date":"2026-10-15","o":99.2,"h":99.9,"l":98.4,"c":98.8,"spy":99.1,"mix_60_40":99.6}
])";

}  // namespace

TEST(chart_illustrative_is_deterministic_and_starts_at_100) {
  auto a = fc::illustrativeSeries(7, 30);
  auto b = fc::illustrativeSeries(7, 30);
  auto other = fc::illustrativeSeries(8, 30);
  CHECK_EQ(a.size(), std::size_t(30));
  CHECK_EQ(a.front().o, 100.0);
  bool same = true, differs = false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    same = same && a[i].c == b[i].c && a[i].spy == b[i].spy;
    differs = differs || a[i].c != other[i].c;
  }
  CHECK(same);
  CHECK(differs);
}

TEST(chart_illustrative_candles_are_consistent) {
  for (const Candle& c : fc::illustrativeSeries(42, 120)) {
    CHECK(c.h >= std::max(c.o, c.c));
    CHECK(c.l <= std::min(c.o, c.c));
    CHECK(c.l > 20.0 && c.h < 500.0);
  }
}

TEST(chart_parse_accepts_valid_feed) {
  std::vector<Candle> series;
  std::string error;
  CHECK(fc::parseHistory(parse(kFeed), series, error));
  CHECK_EQ(series.size(), std::size_t(3));
  CHECK_EQ(series[1].label, std::string("2026-10-14"));
  CHECK_EQ(series[2].mix, 99.6);
}

TEST(chart_parse_close_only_becomes_flat_candles) {
  std::vector<Candle> series;
  std::string error;
  Json feed = parse(R"([{"date":"2026-10-13","c":100.5,"spy":100.1,"mix_60_40":100.2},
                        {"date":"2026-10-14","c":101.0,"spy":100.4,"mix_60_40":100.3}])");
  CHECK(fc::parseHistory(feed, series, error));
  CHECK_EQ(series[0].o, 100.5);
  CHECK_EQ(series[0].h, 100.5);
  CHECK_EQ(series[0].l, 100.5);
}

TEST(chart_parse_repairs_inverted_high_low) {
  std::vector<Candle> series;
  std::string error;
  Json feed = parse(R"([{"date":"2026-10-13","o":101,"h":100,"l":102,"c":100.5,"spy":100,"mix_60_40":100},
                        {"date":"2026-10-14","c":100,"spy":100,"mix_60_40":100}])");
  CHECK(fc::parseHistory(feed, series, error));
  CHECK_EQ(series[0].h, 101.0);
  CHECK_EQ(series[0].l, 100.5);
}

TEST(chart_parse_rejects_bad_feeds) {
  std::vector<Candle> series;
  std::string error;
  CHECK(!fc::parseHistory(parse("{}"), series, error));
  CHECK(!fc::parseHistory(parse("[]"), series, error));
  CHECK(!fc::parseHistory(parse(R"([{"date":"2026-10-13","c":100,"spy":100,"mix_60_40":100}])"), series, error));  // one day
  CHECK(!fc::parseHistory(parse(R"([{"date":"<b>x</b>","c":100,"spy":100,"mix_60_40":100},
                                    {"date":"2026-10-14","c":100,"spy":100,"mix_60_40":100}])"), series, error));
  CHECK(!fc::parseHistory(parse(R"([{"date":"2026-10-14","c":100,"spy":100,"mix_60_40":100},
                                    {"date":"2026-10-13","c":100,"spy":100,"mix_60_40":100}])"), series, error));  // unsorted
  CHECK(!fc::parseHistory(parse(R"([{"date":"2026-10-13","c":100,"spy":100,"mix_60_40":null},
                                    {"date":"2026-10-14","c":100,"spy":100,"mix_60_40":100}])"), series, error));
}

TEST(chart_parse_rejects_values_that_are_not_indexed) {
  // Raw dollar equity must never be plotted: the feed is meant to be indexed to 100.
  std::vector<Candle> series;
  std::string error;
  Json feed = parse(R"([{"date":"2026-10-13","c":102345.12,"spy":100,"mix_60_40":100},
                        {"date":"2026-10-14","c":102400.00,"spy":100,"mix_60_40":100}])");
  CHECK(!fc::parseHistory(feed, series, error));
}

TEST(chart_render_produces_svg_table_and_points) {
  std::vector<Candle> series;
  std::string error;
  CHECK(fc::parseHistory(parse(kFeed), series, error));
  fc::ChartOptions opt;
  Json r = fc::renderChart(series, opt);
  const std::string& svg = r.at("svg").asString();
  CHECK(svg.rfind("<svg", 0) == 0);
  CHECK(contains(svg, "role=\"img\""));
  CHECK(contains(svg, "<title id=\"fcz-t\">"));
  CHECK(contains(svg, "fcz-body up"));
  CHECK(contains(svg, "fcz-body dn"));
  CHECK(contains(svg, "fcz-line spy"));
  CHECK(contains(svg, "fcz-line mix"));
  CHECK(!contains(svg, "ILLUSTRATIVE"));
  CHECK(contains(r.at("table").asString(), "2026-10-15"));
  CHECK_EQ(r.at("points").items().size(), std::size_t(3));
  CHECK(r.at("points").at(0).at("x").asNumber() < r.at("points").at(2).at("x").asNumber());
}

TEST(chart_render_marks_illustrative_data) {
  Json r = fc::renderChart(fc::illustrativeSeries(7, 20), [] {
    fc::ChartOptions o;
    o.illustrative = true;
    return o;
  }());
  CHECK(contains(r.at("svg").asString(), "ILLUSTRATIVE DATA"));
  CHECK(contains(r.at("table").asString(), "Illustrative series, not real results"));
  CHECK(contains(r.at("summary").asString(), "Illustrative data"));
}

TEST(chart_render_compact_has_no_text) {
  fc::ChartOptions o;
  o.compact = true;
  o.width = 160;
  o.height = 64;
  Json r = fc::renderChart(fc::illustrativeSeries(3, 24), o);
  CHECK(!contains(r.at("svg").asString(), "<text"));
  CHECK(contains(r.at("svg").asString(), "aria-hidden=\"true\""));
}

TEST(chart_render_escapes_text_and_ids) {
  fc::ChartOptions o;
  o.title = "<script>alert(1)</script>";
  Json r = fc::renderChart(fc::illustrativeSeries(1, 10), o);
  CHECK(!contains(r.at("svg").asString(), "<script>"));
  CHECK(contains(r.at("svg").asString(), "&lt;script&gt;"));

  // The id prefix goes through the API, which strips anything unsafe.
  Json reply = parse(fc::call("chart.illustrative", R"({"idPrefix":"a\"><img src=x onerror=1>","days":5})"));
  CHECK(reply.at("ok").asBool());
  CHECK(!contains(reply.at("result").at("svg").asString(), "<img"));
}

TEST(chart_api_live_and_errors) {
  Json ok = parse(fc::call("chart.live", std::string(R"({"series":)") + kFeed + "}"));
  CHECK(ok.at("ok").asBool());
  CHECK_EQ(ok.at("result").at("n").asNumber(), 3.0);

  Json bad = parse(fc::call("chart.live", R"({"series":[]})"));
  CHECK(!bad.at("ok").asBool());
  CHECK(contains(bad.at("error").asString(), "2 trading days"));
}

TEST(chart_end_labels_are_dropped_when_lines_converge) {
  // Bot, SPY and 60/40 all close within a pixel of each other: only the bot label survives.
  std::vector<Candle> series;
  std::string error;
  Json feed = parse(R"([{"date":"2026-10-13","c":100,"spy":100,"mix_60_40":100},
                        {"date":"2026-10-14","c":100.01,"spy":100.02,"mix_60_40":100.03}])");
  CHECK(fc::parseHistory(feed, series, error));
  Json rendered = fc::renderChart(series, fc::ChartOptions{});
  const std::string& svg = rendered.at("svg").asString();
  CHECK(contains(svg, ">Bot "));
  CHECK(!contains(svg, ">SPY "));
  CHECK(!contains(svg, ">60/40 "));
}
