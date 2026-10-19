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

size_t count(const std::string& hay, const std::string& needle) {
  size_t n = 0;
  for (size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1)) ++n;
  return n;
}

TEST(chart_y_axis_has_exactly_five_ticks_and_x_axis_three_labels) {
  std::vector<Candle> live;
  std::string error;
  CHECK(fc::parseHistory(parse(kFeed), live, error));
  for (const auto& series : {fc::illustrativeSeries(7, 30), fc::illustrativeSeries(3, 8), live}) {
    for (int width : {720, 360}) {
      fc::ChartOptions o;
      o.width = width;
      o.height = width == 720 ? 360 : 300;
      const std::string svg = fc::renderChart(series, o).at("svg").asString();
      CHECK_EQ(count(svg, "class=\"fcz-grid\""), size_t(5));
      CHECK_EQ(count(svg, "text-anchor=\"end\">"), size_t(6));  // five y tick labels and the last day on the x axis
      CHECK_EQ(count(svg, "text-anchor=\"start\">"), size_t(1));
      CHECK_EQ(count(svg, "text-anchor=\"middle\">"), size_t(1));
    }
  }
}

TEST(chart_every_day_has_a_readout_sentence_and_an_excess_value) {
  std::vector<Candle> live;
  std::string error;
  CHECK(fc::parseHistory(parse(kFeed), live, error));
  Json r = fc::renderChart(live, fc::ChartOptions{});
  const Json& p1 = r.at("points").at(1);
  CHECK_EQ(p1.at("ex").asNumber(), 0.7);  // 100.0 - 99.3
  const std::string sentence = p1.at("readout").asString();
  CHECK(sentence.rfind("Day 2 (2026-10-14). The bot opened at 99.8 and closed at 100.0 (+0.20%), ", 0) == 0);
  CHECK(contains(sentence, "0.7 points ahead of the S&P 500 at 99.3."));
  CHECK(!contains(sentence, "Illustrative"));
  CHECK(contains(r.at("points").at(2).at("readout").asString(), "(\xE2\x88\x92" "0.40%)"));  // a down day uses a real minus sign
  CHECK(contains(r.at("points").at(2).at("readout").asString(), "0.3 points behind the S&P 500 at 99.1."));

  // While the data is illustrative the sentence itself says so.
  fc::ChartOptions o;
  o.illustrative = true;
  Json demo = fc::renderChart(fc::illustrativeSeries(7, 24), o);
  for (const Json& p : demo.at("points").items()) CHECK(p.at("readout").asString().rfind("Illustrative. Day ", 0) == 0);
  CHECK(contains(demo.at("points").at(23).at("readout").asString(), "Day 24."));
}

TEST(chart_readout_says_level_when_the_bot_matches_the_market) {
  std::vector<Candle> series;
  std::string error;
  Json feed = parse(R"([{"date":"2026-10-13","c":100,"spy":100,"mix_60_40":100},
                        {"date":"2026-10-14","c":100.02,"spy":100.0,"mix_60_40":100}])");
  CHECK(fc::parseHistory(feed, series, error));
  Json r = fc::renderChart(series, fc::ChartOptions{});
  CHECK(contains(r.at("points").at(1).at("readout").asString(), "level with the S&P 500 at 100.0."));
}

TEST(chart_excess_strip_is_optional_and_has_one_bar_per_day) {
  std::vector<Candle> live;
  std::string error;
  CHECK(fc::parseHistory(parse(kFeed), live, error));
  Json without = fc::renderChart(live, fc::ChartOptions{});
  CHECK(without.find("excess") == nullptr);

  fc::ChartOptions o;
  o.excessHeight = 76;
  Json r = fc::renderChart(live, o);
  const std::string& ex = r.at("excess").asString();
  CHECK(contains(ex, "role=\"img\""));
  CHECK(contains(ex, "viewBox=\"0 0 720 76\""));
  CHECK_EQ(count(ex, "<rect class=\"fcz-ex "), size_t(3));
  CHECK_EQ(count(ex, "fcz-ex pos"), size_t(2));  // 0.2 and 0.7 ahead of SPY
  CHECK_EQ(count(ex, "fcz-ex neg"), size_t(1));  // 98.8 against 99.1
  CHECK(contains(ex, "class=\"fcz-zero\""));
  CHECK(r.at("exPlot").at("h").asNumber() == 76.0);

  o.illustrative = true;
  CHECK(contains(fc::renderChart(fc::illustrativeSeries(7, 12), o).at("excess").asString(), "Illustrative data, not real results."));

  // Compact previews never get a strip.
  o.compact = true;
  CHECK(fc::renderChart(live, o).find("excess") == nullptr);
}

TEST(chart_api_passes_the_strip_height_through) {
  Json reply = parse(fc::call("chart.illustrative", R"({"days":10,"excessHeight":76})"));
  CHECK(reply.at("ok").asBool());
  CHECK(reply.at("result").find("excess") != nullptr);
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
