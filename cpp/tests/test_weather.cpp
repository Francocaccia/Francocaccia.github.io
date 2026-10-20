#include <algorithm>
#include <cmath>

#include "fc/api.hpp"
#include "fc/geo.hpp"
#include "fc/json.hpp"
#include "fc/weather.hpp"
#include "test.hpp"

using fc::Json;

namespace {

bool has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

Json parse(const std::string& text) {
  Json out;
  std::string error;
  bool ok = Json::parse(text, out, error);
  CHECK(ok);
  return out;
}

// Value of a decoded-table row, with several rows of the same label joined by "|".
std::string row(const Json& report, const char* label) {
  std::string out;
  for (const Json& r : report.at("rows").items()) {
    if (r.at("label").asString() == label) out += (out.empty() ? "" : "|") + r.at("value").asString();
  }
  return out;
}

bool hasGroup(const Json& report, const char* kind, const char* text) {
  for (const Json& g : report.at("groups").items()) {
    if (g.at("kind").asString() == kind && g.at("text").asString() == text) return true;
  }
  return false;
}

Json metar(const std::string& raw) {
  Json r = fc::decodeMetar(raw);
  CHECK(r.at("ok").asBool());
  return r;
}

Json taf(const std::string& raw, double monthDays = 31) {
  Json in = Json::object();
  in.set("raw", raw).set("monthDays", monthDays);
  return fc::decodeTaf(in);
}

}  // namespace

// ------------------------------------------------------------------------ METAR

TEST(metar_real_kabe_report) {
  // Fetched from aviationweather.gov for Lehigh Valley.
  Json r = metar("METAR KABE 181451Z 00000KT 10SM CLR 09/M01 A3062 RMK AO2 SLP371 T00891011 52015 $");
  CHECK_EQ(r.at("station").asString(), std::string("KABE"));
  CHECK_EQ(r.at("category").asString(), std::string("VFR"));
  CHECK(r.at("wind").at("calm").asBool());
  CHECK_EQ(r.at("tempC").asNumber(), 9.0);
  CHECK_EQ(r.at("dewC").asNumber(), -1.0);
  CHECK_EQ(r.at("inHg").asNumber(), 30.62);
  CHECK(std::fabs(r.at("hpa").asNumber() - 1036.9) < 0.2);
  CHECK_EQ(r.at("slpHpa").asNumber(), 1037.1);
  CHECK(std::fabs(r.at("preciseTempC").asNumber() - 8.9) < 1e-9);
  CHECK(std::fabs(r.at("preciseDewC").asNumber() - (-1.1)) < 1e-9);
  CHECK(std::fabs(r.at("humidity").asNumber() - 49.6) < 1.0);
  CHECK(has(row(r, "Remarks"), "Sea-level pressure 1037.1 hPa"));
  CHECK(has(row(r, "Remarks"), "Precise temperature 8.9"));
  CHECK(has(row(r, "Remarks"), "rising, +1.5 hPa"));
  CHECK(has(row(r, "Remarks"), "maintenance"));
  CHECK(has(row(r, "Clouds"), "Clear below 12,000 ft"));
  CHECK(has(row(r, "Pressure"), "30.62 inHg = 1036.9 hPa"));
  CHECK(hasGroup(r, "type", "METAR"));
  CHECK(hasGroup(r, "wind", "00000KT"));
  CHECK(hasGroup(r, "temp", "09/M01"));
  CHECK(hasGroup(r, "qnh", "A3062"));
  CHECK(r.at("warnings").items().empty());
}

TEST(metar_real_sabe_report_with_variable_wind_and_cavok) {
  Json r = metar("METAR SABE 181500Z 08007KT 050V110 CAVOK 13/03 Q1022 NOSIG");
  CHECK_EQ(r.at("category").asString(), std::string("VFR"));
  CHECK_EQ(r.at("wind").at("dir").asNumber(), 80.0);
  CHECK_EQ(r.at("wind").at("speedKt").asNumber(), 7.0);
  CHECK(has(row(r, "Wind (true direction)"), "From 80\xC2\xB0 (east) at 7 kt"));
  CHECK(has(row(r, "Wind (true direction)"), "varying between 50\xC2\xB0 and 110\xC2\xB0"));
  CHECK(hasGroup(r, "windvar", "050V110"));
  CHECK(hasGroup(r, "vis", "CAVOK"));
  CHECK(hasGroup(r, "trend", "NOSIG"));
  CHECK_EQ(r.at("hpa").asNumber(), 1022.0);
  CHECK_EQ(r.at("inHg").asNumber(), 30.18);
  CHECK(has(row(r, "Trend"), "No significant change"));
  CHECK(std::fabs(r.at("humidity").asNumber() - 50.7) < 1.0);
  CHECK_EQ(r.at("cloudBaseFt").asNumber(), 4000.0);  // 10 degrees of spread
  CHECK(has(row(r, "Cloud base estimate (approx.)"), "About 4,000 ft AGL"));
}

TEST(metar_correction_flag_after_the_time_group) {
  // Exactly as aviationweather.gov returned it for Buenos Aeroparque.
  Json r = metar("METAR SABE 181500Z COR 08007KT 050V110 CAVOK 13/03 Q1022 NOSIG");
  CHECK(r.at("warnings").items().empty());
  CHECK(has(row(r, "Report"), "corrected"));
  CHECK_EQ(r.at("category").asString(), std::string("VFR"));
}

TEST(metar_ifr_with_gusts_fraction_visibility_and_weather) {
  Json r = metar("KBOS 051156Z 05012G25KT 1 1/2SM -RA BR OVC008 09/08 A2981 RMK AO2 SLP102 P0012 T00940083");
  CHECK_EQ(r.at("category").asString(), std::string("IFR"));
  CHECK_EQ(r.at("wind").at("gustKt").asNumber(), 25.0);
  CHECK(hasGroup(r, "vis", "1 1/2SM"));
  CHECK(has(row(r, "Visibility"), "1.5 SM (2,414 m)"));
  CHECK_EQ(row(r, "Weather"), std::string("Light rain\nMist"));
  CHECK_EQ(r.at("ceilingFt").asNumber(), 800.0);
  CHECK(has(row(r, "Wind (true direction)"), "gusting 25 kt"));
  CHECK(has(row(r, "Remarks"), "Precipitation in the last hour: 0.12 in"));
}

TEST(metar_lifr_calm_fog_with_vertical_visibility) {
  Json r = metar("KSFO 011756Z 00000KT 1/4SM FG VV002 12/12 A3010 RMK AO2");
  CHECK_EQ(r.at("category").asString(), std::string("LIFR"));
  CHECK(has(row(r, "Clouds"), "vertical visibility 200 ft"));
  CHECK_EQ(row(r, "Weather"), std::string("Fog"));
  CHECK(has(row(r, "Cloud base estimate (approx.)"), "saturated"));
  CHECK_EQ(r.at("humidity").asNumber(), 100.0);
}

TEST(metar_mvfr_from_ceiling_and_visibility) {
  Json r = metar("KORD 221951Z 24015G28KT 4SM BR BKN020 OVC035 16/14 A2990");
  CHECK_EQ(r.at("category").asString(), std::string("MVFR"));
  CHECK_EQ(r.at("ceilingFt").asNumber(), 2000.0);
  CHECK(has(row(r, "Clouds"), "Broken clouds (5-7 oktas) at 2,000 ft AGL (610 m)"));
  CHECK(has(row(r, "Clouds"), "Overcast (8 oktas) at 3,500 ft AGL"));
}

TEST(metar_visibility_shows_the_reports_own_unit_first) {
  CHECK_EQ(row(metar("KABE 181451Z 27008KT 10SM FEW050 18/09 A3002"), "Visibility"), std::string("10 SM (16 km)"));
  CHECK_EQ(row(metar("KABE 181451Z 27008KT P6SM FEW050 18/09 A3002"), "Visibility"), std::string("More than 6 SM (9.7 km)"));
  CHECK_EQ(row(metar("KABE 181451Z 27008KT M1/4SM FG VV001 18/18 A3002"), "Visibility"), std::string("Less than 0.25 SM (402 m)"));
  CHECK_EQ(row(metar("LFPG 161830Z 24005KT 5000 FEW040 15/09 Q1018"), "Visibility"), std::string("5.0 km (3.1 SM)"));
}

TEST(metar_category_boundaries) {
  // FAA: ceiling 3000 is still MVFR, 3100 is VFR; visibility 5 SM is MVFR, 6 SM is VFR.
  CHECK_EQ(metar("KXYZ 011200Z 00000KT 10SM BKN030 10/05 A3000").at("category").asString(), std::string("MVFR"));
  CHECK_EQ(metar("KXYZ 011200Z 00000KT 10SM BKN031 10/05 A3000").at("category").asString(), std::string("VFR"));
  CHECK_EQ(metar("KXYZ 011200Z 00000KT 5SM SKC 10/05 A3000").at("category").asString(), std::string("MVFR"));
  CHECK_EQ(metar("KXYZ 011200Z 00000KT 6SM SKC 10/05 A3000").at("category").asString(), std::string("VFR"));
  CHECK_EQ(metar("KXYZ 011200Z 00000KT 10SM OVC010 10/05 A3000").at("category").asString(), std::string("MVFR"));
  CHECK_EQ(metar("KXYZ 011200Z 00000KT 10SM OVC009 10/05 A3000").at("category").asString(), std::string("IFR"));
  CHECK_EQ(metar("KXYZ 011200Z 00000KT 10SM OVC004 10/05 A3000").at("category").asString(), std::string("LIFR"));
  CHECK_EQ(metar("KXYZ 011200Z 00000KT 3SM SKC 10/05 A3000").at("category").asString(), std::string("MVFR"));
  CHECK_EQ(metar("KXYZ 011200Z 00000KT 2SM SKC 10/05 A3000").at("category").asString(), std::string("IFR"));
  CHECK_EQ(metar("KXYZ 011200Z 00000KT 1/2SM SKC 10/05 A3000").at("category").asString(), std::string("LIFR"));
  // FEW and SCT layers are not ceilings.
  CHECK_EQ(metar("KXYZ 011200Z 00000KT 10SM FEW005 SCT008 10/05 A3000").at("category").asString(), std::string("VFR"));
}

TEST(metar_icao_thunderstorm_cumulonimbus_trend_and_wind_variation) {
  Json r = metar("LFPG 161830Z 24015G30KT 190V270 3000 +TSRA SCT008 BKN025CB 18/17 Q1005 TEMPO 1500 BKN010");
  CHECK_EQ(r.at("category").asString(), std::string("IFR"));
  CHECK_EQ(row(r, "Weather"), std::string("Heavy thunderstorm with rain"));
  CHECK(has(row(r, "Clouds"), "cumulonimbus"));
  CHECK(has(row(r, "Visibility"), "3,000 m (1.9 SM)"));
  CHECK(has(row(r, "Trend"), "Temporarily"));
  CHECK(has(row(r, "Trend"), "1,500 m"));
  CHECK(hasGroup(r, "cloud", "BKN025CB"));
  CHECK(hasGroup(r, "wx", "+TSRA"));
  CHECK_EQ(r.at("ceilingFt").asNumber(), 2500.0);  // SCT008 is not a ceiling
}

TEST(metar_negative_temperatures_and_altimeter_conversion) {
  Json r = metar("CYYZ 111500Z 32012KT 6SM -SN BKN015 M02/M04 A2995");
  CHECK_EQ(r.at("tempC").asNumber(), -2.0);
  CHECK_EQ(r.at("dewC").asNumber(), -4.0);
  CHECK_EQ(r.at("inHg").asNumber(), 29.95);
  CHECK(std::fabs(r.at("hpa").asNumber() - 1014.2) < 0.1);
  CHECK_EQ(r.at("category").asString(), std::string("MVFR"));
  CHECK(has(row(r, "Temperature"), "-2.0 \xC2\xB0" "C (28.4 \xC2\xB0" "F)"));
}

TEST(metar_speci_auto_and_cor_prefixes) {
  Json r = metar("SPECI KDEN 151822Z AUTO 36025G40KT 1/2SM +SN VV003 M05/M06 A2980 RMK AO2");
  CHECK(has(row(r, "Report"), "SPECI"));
  CHECK(has(row(r, "Report"), "automated"));
  CHECK_EQ(r.at("category").asString(), std::string("LIFR"));
  CHECK_EQ(row(r, "Weather"), std::string("Heavy snow"));
  Json cor = metar("METAR COR EGLL 161720Z 24005KT 9999 FEW040 15/09 Q1018");
  CHECK(has(row(cor, "Report"), "corrected"));
  CHECK_EQ(cor.at("station").asString(), std::string("EGLL"));
  CHECK_EQ(row(cor, "Visibility"), std::string("10 km or more"));
}

TEST(metar_accepts_lowercase_and_trailing_equals) {
  Json r = metar("metar kabe 181451z 27008kt 10sm few050 18/09 a3002=");
  CHECK_EQ(r.at("station").asString(), std::string("KABE"));
  CHECK(r.at("warnings").items().empty());
  CHECK(std::fabs(r.at("hpa").asNumber() - 1016.6) < 0.1);
}

TEST(metar_units_mps_and_metres) {
  Json r = metar("UUEE 181430Z 18006MPS 9999 BKN033 08/04 Q1015");
  CHECK_EQ(r.at("wind").at("speedKt").asNumber(), 12.0);  // 6 m/s
  CHECK(has(row(r, "Wind (true direction)"), "at 12 kt (6.0 m/s"));
  Json low = metar("LFPG 161830Z 24005KT 0800 FG VV001 05/05 Q1020");
  CHECK(has(row(low, "Visibility"), "800 m (0.50 SM)"));
  CHECK_EQ(low.at("category").asString(), std::string("LIFR"));
}

TEST(metar_runway_visual_range) {
  Json r = metar("LFPG 161830Z 24005KT 0400 R26L/0600V1000N FG VV001 05/05 Q1020");
  CHECK(hasGroup(r, "rvr", "R26L/0600V1000N"));
  CHECK(has(row(r, "Runway / shear"), "Runway 26L: visual range 600 m varying to 1000 m, no change"));
}

TEST(metar_us_remarks) {
  Json r = metar("KJFK 121651Z 28020G35KT 10SM FEW250 15/05 A3003 RMK AO2 PK WND 28038/1620 WSHFT 1530 FROPA SLP168 T01500050 10156 20094 PRESFR");
  std::string remarks = row(r, "Remarks");
  CHECK(has(remarks, "Peak wind from 280\xC2\xB0 at 38 kt, at 16:20 UTC"));
  CHECK(has(remarks, "Wind shift at 15:30 UTC (frontal passage)"));
  CHECK(has(remarks, "6-hour maximum temperature 15.6"));
  CHECK(has(remarks, "6-hour minimum temperature 9.4"));
  CHECK(has(remarks, "Pressure falling rapidly"));
  CHECK_EQ(r.at("slpHpa").asNumber(), 1016.8);
  CHECK_EQ(metar("KJFK 121651Z 28005KT 10SM FEW250 15/05 A3003 RMK AO2 SLP982").at("slpHpa").asNumber(), 998.2);
}

TEST(metar_wind_shear_and_recent_weather) {
  Json r = metar("EGLL 161720Z 24015KT 9999 WS RWY27 FEW040 15/09 Q1018 RERA");
  CHECK(has(row(r, "Runway / shear"), "Wind shear on runway 27"));
  CHECK(hasGroup(r, "wx", "RERA"));
}

TEST(metar_unknown_groups_warn_but_do_not_fail) {
  Json r = metar("KABE 181451Z 27008KT 10SM FEW050 XYZZY 18/09 A3002");
  CHECK_EQ(r.at("warnings").items().size(), std::size_t(1));
  CHECK(hasGroup(r, "unknown", "XYZZY"));
  CHECK_EQ(r.at("tempC").asNumber(), 18.0);
}

TEST(metar_rejects_unusable_input) {
  CHECK(!fc::decodeMetar("").at("ok").asBool());
  CHECK(!fc::decodeMetar("   ").at("ok").asBool());
  CHECK(has(fc::decodeMetar("TAF KABE 161720Z 1618/1724 27008KT P6SM FEW050").at("error").asString(), "TAF"));
  CHECK(has(fc::decodeMetar("hello there").at("error").asString(), "station"));
  CHECK(has(fc::decodeMetar("KABE 27008KT 10SM").at("error").asString(), "observation time"));
  CHECK(has(fc::decodeMetar("KABE 991451Z 27008KT").at("error").asString(), "not valid"));
}

TEST(metar_output_is_escaped) {
  Json r = fc::decodeMetar("KABE 181451Z 27008KT <script>alert(1)</script> 10SM \"onmouseover=x\" A3002");
  CHECK(r.at("ok").asBool());
  const std::string& html = r.at("html").asString();
  CHECK(!has(html, "<script>"));
  CHECK(has(html, "&lt;SCRIPT&gt;"));
  CHECK(!has(html, "onmouseover=x\""));
  CHECK(has(html, "data-tip=\""));
  CHECK(!has(html, "tabindex"));  // the page adds a roving tabindex, so each report line is one Tab stop
}

TEST(metar_observed_time_is_echoed_when_given) {
  Json r = fc::decodeMetar("KABE 181451Z 27008KT 10SM FEW050 18/09 A3002", "2026-10-18 14:51 UTC");
  CHECK(has(row(r, "Observed"), "2026-10-18 14:51 UTC"));
  CHECK(has(row(r, "Observed"), "Day 18 at 14:51 UTC"));
}

// -------------------------------------------------------------------------- TAF

const char* kSampleTaf =
    "TAF KABE 161720Z 1618/1724 27008KT P6SM FEW050 BKN250 FM162000 28010G18KT P6SM SCT040 "
    "TEMPO 1700/1704 3SM -SHRA BKN020 FM170600 18006KT 5SM BR OVC008 PROB30 1709/1712 1SM FG VV002 "
    "BECMG 1712/1714 27012KT P6SM SCT050 TX22/1718Z TN08/1710Z";

TEST(taf_periods_timing_and_categories) {
  Json t = taf(kSampleTaf);
  CHECK(t.at("ok").asBool());
  CHECK_EQ(t.at("station").asString(), std::string("KABE"));
  CHECK_EQ(t.at("validityHours").asNumber(), 30.0);
  const auto& p = t.at("periods").items();
  CHECK_EQ(p.size(), std::size_t(6));
  const char* kinds[] = {"base", "from", "tempo", "from", "prob", "becmg"};
  const double start[] = {0, 2, 6, 12, 15, 18};
  const double end[] = {2, 12, 10, 30, 18, 20};
  const char* cats[] = {"VFR", "VFR", "MVFR", "IFR", "LIFR", "VFR"};
  for (std::size_t i = 0; i < 6 && i < p.size(); ++i) {
    CHECK_EQ(p[i].at("kind").asString(), std::string(kinds[i]));
    CHECK_EQ(p[i].at("start").asNumber(), start[i]);
    CHECK_EQ(p[i].at("end").asNumber(), end[i]);
    CHECK_EQ(p[i].at("category").asString(), std::string(cats[i]));
  }
  CHECK_EQ(p[4].at("prob").asNumber(), 30.0);
}

TEST(taf_decoded_text_and_html) {
  Json t = taf(kSampleTaf);
  const std::string& html = t.at("html").asString();
  CHECK(has(html, "tf-list"));
  CHECK(has(html, "From day 16 20:00 UTC"));
  CHECK(has(html, "Temporarily, day 17 00:00 to day 17 04:00 UTC"));
  CHECK(has(html, "30% chance"));
  CHECK(has(html, "Forecast maximum 22"));
  CHECK(has(html, "Forecast minimum 8"));
  CHECK(has(html, "Light rain showers"));
  CHECK(has(html, "gusting 18 kt"));
  CHECK(has(html, "Fog"));
  CHECK(has(html, "vertical visibility 200 ft"));
  CHECK(has(html, "cat-lifr"));
  CHECK(t.at("warnings").items().empty());
}

TEST(taf_timeline_svg) {
  Json t = taf(kSampleTaf);
  const std::string& svg = t.at("timeline").asString();
  CHECK(svg.rfind("<svg", 0) == 0);
  CHECK(has(svg, "role=\"img\""));
  CHECK(has(svg, "tl-bar cat-lifr tl-prob"));
  CHECK(has(svg, "tl-bar cat-ifr tl-from"));
  CHECK(has(svg, "d16 18Z"));
  CHECK(has(svg, "d17 00Z"));
}

TEST(taf_prob_with_tempo_and_amendments) {
  Json t = taf("TAF AMD KJFK 100500Z 1006/1112 VRB03KT 9999 SCT040 PROB40 TEMPO 1008/1012 3000 TSRA");
  CHECK(t.at("ok").asBool());
  const auto& p = t.at("periods").items();
  CHECK_EQ(p.size(), std::size_t(2));
  CHECK_EQ(p[1].at("kind").asString(), std::string("prob"));
  CHECK_EQ(p[1].at("prob").asNumber(), 40.0);
  CHECK(has(p[1].at("when").asString(), "temporarily"));
  CHECK(has(t.at("html").asString(), "Amended forecast"));
  CHECK_EQ(t.at("validityHours").asNumber(), 30.0);
}

TEST(taf_crossing_a_month_end) {
  Json t = taf("TAF KABE 302300Z 3100/0106 27008KT P6SM SCT050 TEMPO 0100/0104 3SM RA", 31);
  CHECK(t.at("ok").asBool());
  CHECK_EQ(t.at("validityHours").asNumber(), 30.0);
  CHECK_EQ(t.at("periods").at(std::size_t(1)).at("start").asNumber(), 24.0);
  CHECK_EQ(t.at("periods").at(std::size_t(1)).at("end").asNumber(), 28.0);
}

TEST(taf_rejects_unusable_input) {
  CHECK(!fc::decodeTaf(parse(R"({"raw":""})")).at("ok").asBool());
  CHECK(has(fc::decodeTaf(parse(R"({"raw":"KABE 181451Z 27008KT 10SM"})")).at("error").asString(), "validity"));
  CHECK(has(fc::decodeTaf(parse(R"({"raw":"METAR KABE 181451Z 27008KT 10SM"})")).at("error").asString(), "METAR"));
  CHECK(has(fc::decodeTaf(parse(R"({"raw":"TAF KABE 161720Z NIL"})")).at("error").asString(), "NIL"));
  Json odd = taf("TAF KABE 161720Z 1618/1724 27008KT P6SM QQQQ FEW050");
  CHECK(odd.at("ok").asBool());
  CHECK_EQ(odd.at("warnings").items().size(), std::size_t(1));
}

TEST(taf_output_is_escaped) {
  Json t = taf("TAF KABE 161720Z 1618/1724 27008KT <img/src=x/onerror=alert(1)> P6SM FEW050");
  CHECK(t.at("ok").asBool());
  CHECK(!has(t.at("html").asString(), "<img"));
  CHECK(has(t.at("html").asString(), "&lt;IMG"));
}

// ------------------------------------------------------------------- crosswind

namespace {
Json wind(double dir, double speed, const char* runway, double gust = -1, double variation = 0) {
  Json in = Json::object();
  in.set("dir", dir).set("speed", speed).set("runway", runway).set("variation", variation);
  if (gust >= 0) in.set("gust", gust);
  return fc::windComponents(in);
}
}  // namespace

TEST(crosswind_basic_components) {
  Json straight = wind(270, 10, "27");
  CHECK(std::fabs(straight.at("headwind").asNumber() - 10) < 0.05);
  CHECK(std::fabs(straight.at("crosswind").asNumber()) < 0.05);
  CHECK_EQ(straight.at("crossText").asString(), std::string("No crosswind"));
  CHECK_EQ(straight.at("headText").asString(), std::string("Headwind 10 kt"));

  Json fromRight = wind(360, 10, "27");  // north wind, heading west: from the right
  CHECK(std::fabs(fromRight.at("crosswind").asNumber() - 10) < 0.05);
  CHECK_EQ(fromRight.at("crossText").asString(), std::string("Crosswind 10 kt from the right"));
  CHECK_EQ(fromRight.at("headText").asString(), std::string("No headwind or tailwind"));

  Json fromLeft = wind(180, 20, "27");
  CHECK_EQ(fromLeft.at("crossText").asString(), std::string("Crosswind 20 kt from the left"));

  Json tail = wind(90, 10, "27");
  CHECK_EQ(tail.at("headText").asString(), std::string("Tailwind 10 kt"));

  Json angled = wind(240, 20, "27");  // 30 degrees off the nose
  CHECK_EQ(angled.at("headText").asString(), std::string("Headwind 17 kt"));
  CHECK_EQ(angled.at("crossText").asString(), std::string("Crosswind 10 kt from the left"));
  CHECK_EQ(angled.at("runwayHeading").asNumber(), 270.0);
}

TEST(crosswind_magnetic_variation_and_gusts) {
  Json r = wind(270, 10, "27", -1, 10);  // 10 degrees east variation: magnetic wind 260
  CHECK_EQ(r.at("windMagnetic").asNumber(), 260.0);
  CHECK_EQ(r.at("crossText").asString(), std::string("Crosswind 2 kt from the left"));
  Json g = wind(300, 10, "27", 20);
  CHECK(g.find("gustCrossText") != nullptr);
  CHECK_EQ(g.at("gustCrossText").asString(), std::string("Crosswind 10 kt from the right"));
  CHECK_EQ(g.at("crossText").asString(), std::string("Crosswind 5 kt from the right"));
}

TEST(crosswind_runway_suffixes_and_validation) {
  CHECK_EQ(wind(90, 5, "09L").at("runwayHeading").asNumber(), 90.0);
  CHECK_EQ(wind(90, 5, "9r").at("runwayHeading").asNumber(), 90.0);
  CHECK_EQ(wind(90, 5, "36").at("runwayHeading").asNumber(), 360.0);
  CHECK(wind(90, 5, "37").find("error") != nullptr);
  CHECK(wind(90, 5, "00").find("error") != nullptr);
  CHECK(wind(90, 5, "abc").find("error") != nullptr);
  CHECK(wind(400, 5, "27").find("error") != nullptr);
  CHECK(wind(90, 5, "27", 3).find("error") != nullptr);  // gust below steady wind
  CHECK(has(wind(90, 5, "27").at("svg").asString(), "xw-svg"));
}

// -------------------------------------------------------------------------- geo

TEST(geo_distance_and_course_match_known_values) {
  fc::LatLon jfk{40.6398, -73.7789}, lhr{51.4700, -0.4543};
  CHECK(std::fabs(fc::distanceNm(jfk, lhr) - 2991.0) < 8.0);  // 5,540 km
  CHECK(std::fabs(fc::initialCourse(jfk, lhr) - 51.4) < 1.0);
  CHECK(std::fabs(fc::distanceNm({0, 0}, {0, 1}) - 60.04) < 0.1);
  CHECK(std::fabs(fc::distanceNm({0, 0}, {1, 0}) - 60.04) < 0.1);
  CHECK(std::fabs(fc::initialCourse({0, 0}, {0, 1}) - 90.0) < 1e-6);
  CHECK(std::fabs(fc::initialCourse({0, 0}, {1, 0}) - 0.0) < 1e-6);
  CHECK(std::fabs(fc::initialCourse({0, 0}, {-1, 0}) - 180.0) < 1e-6);
  CHECK(std::fabs(fc::initialCourse({0, 0}, {0, -1}) - 270.0) < 1e-6);
  CHECK(fc::distanceNm(jfk, jfk) < 1e-9);
}

TEST(geo_intermediate_points_lie_on_the_great_circle) {
  fc::LatLon a{40.6398, -73.7789}, b{51.4700, -0.4543};
  double total = fc::distanceNm(a, b);
  for (double f : {0.0, 0.25, 0.5, 0.9, 1.0}) {
    fc::LatLon p = fc::intermediatePoint(a, b, f);
    CHECK(std::fabs(fc::distanceNm(a, p) - f * total) < 0.5);
    CHECK(std::fabs(fc::distanceNm(a, p) + fc::distanceNm(p, b) - total) < 0.5);
  }
}

const char* kRoute = R"({"airports":[
    {"icao":"SABE","name":"Buenos Aires Aeroparque","lat":-34.5592,"lon":-58.4156},
    {"icao":"KLYH","name":"Lynchburg Regional","lat":37.3267,"lon":-79.2004},
    {"icao":"KABE","name":"Lehigh Valley Intl","lat":40.6522,"lon":-75.4408}]})";

TEST(geo_route_legs_view_and_arcs) {
  Json r = fc::routeGeometry(parse(kRoute));
  CHECK_EQ(r.at("legs").items().size(), std::size_t(2));
  const Json& l1 = r.at("legs").at(std::size_t(0));
  const Json& l2 = r.at("legs").at(std::size_t(1));
  CHECK_EQ(l1.at("from").asString(), std::string("SABE"));
  CHECK(l1.at("nm").asNumber() > 4300 && l1.at("nm").asNumber() < 4700);
  CHECK(l1.at("course").asNumber() > 341 && l1.at("course").asNumber() < 345);  // north-west
  CHECK(l2.at("nm").asNumber() > 255 && l2.at("nm").asNumber() < 275);
  CHECK(l2.at("course").asNumber() > 38 && l2.at("course").asNumber() < 42);    // north-north-east
  CHECK(std::fabs(r.at("totalNm").asNumber() - (l1.at("nm").asNumber() + l2.at("nm").asNumber())) < 0.2);
  CHECK(!r.find("svg"));  // the map is drawn in the browser now

  // The first view is halfway between the two ends of the route (about 3 N, 66.6 W).
  fc::LatLon mid = fc::intermediatePoint({-34.5592, -58.4156}, {40.6522, -75.4408}, 0.5);
  CHECK(std::fabs(r.at("view").at("lat").asNumber() - mid.lat) < 0.05);
  CHECK(std::fabs(r.at("view").at("lon").asNumber() - mid.lon) < 0.05);
  CHECK(r.at("view").at("lat").asNumber() > 2 && r.at("view").at("lat").asNumber() < 4);
  CHECK(r.at("view").at("lon").asNumber() > -68 && r.at("view").at("lon").asNumber() < -65);

  // One arc per leg, 65 samples each (64 steps), ending on the airports and lying on the great circle.
  CHECK_EQ(r.at("arcs").items().size(), std::size_t(2));
  const fc::LatLon ends[3] = {{-34.5592, -58.4156}, {37.3267, -79.2004}, {40.6522, -75.4408}};
  for (std::size_t leg = 0; leg < 2; ++leg) {
    const Json& arc = r.at("arcs").at(leg);
    CHECK_EQ(arc.items().size(), std::size_t(65));
    auto at = [&](std::size_t i) { return fc::LatLon{arc.at(i).at(std::size_t(0)).asNumber(), arc.at(i).at(std::size_t(1)).asNumber()}; };
    CHECK(std::fabs(at(0).lat - ends[leg].lat) < 1e-3 && std::fabs(at(0).lon - ends[leg].lon) < 1e-3);
    CHECK(std::fabs(at(64).lat - ends[leg + 1].lat) < 1e-3 && std::fabs(at(64).lon - ends[leg + 1].lon) < 1e-3);
    double whole = fc::distanceNm(ends[leg], ends[leg + 1]);
    for (std::size_t i = 0; i <= 64; ++i) {
      fc::LatLon pt = at(i);
      CHECK(std::isfinite(pt.lat) && std::isfinite(pt.lon));
      CHECK(std::fabs(fc::distanceNm(ends[leg], pt) + fc::distanceNm(pt, ends[leg + 1]) - whole) < 0.5);
    }
  }
}

TEST(geo_route_guards_bad_input) {
  // No NaN for identical points; antipodal and out-of-range input are refused with a message.
  Json same = parse(fc::call("geo.route", R"({"airports":[{"icao":"A","lat":10,"lon":20},{"icao":"B","lat":10,"lon":20}]})"));
  CHECK(same.at("ok").asBool());
  CHECK_EQ(same.at("result").at("legs").at(std::size_t(0)).at("nm").asNumber(), 0.0);
  CHECK(std::isfinite(same.at("result").at("arcs").at(std::size_t(0)).at(std::size_t(10)).at(std::size_t(0)).asNumber()));
  CHECK(!parse(fc::call("geo.route", R"({"airports":[{"icao":"A","lat":10,"lon":20},{"icao":"B","lat":-10,"lon":-160}]})")).at("ok").asBool());
  CHECK(!parse(fc::call("geo.route", R"({"airports":[{"icao":"A","lat":91,"lon":0},{"icao":"B","lat":0,"lon":10}]})")).at("ok").asBool());
  CHECK(!parse(fc::call("geo.route", R"({"airports":[{"icao":"A","lat":0,"lon":181},{"icao":"B","lat":0,"lon":10}]})")).at("ok").asBool());
  CHECK(!parse(fc::call("geo.route", R"({"airports":[{"icao":"A","lat":"x","lon":0},{"icao":"B","lat":0,"lon":10}]})")).at("ok").asBool());
  // The sample count is clamped, never trusted.
  Json few = parse(fc::call("geo.route", R"({"samples":-5,"airports":[{"icao":"A","lat":0,"lon":0},{"icao":"B","lat":0,"lon":10}]})"));
  CHECK_EQ(few.at("result").at("arcs").at(std::size_t(0)).items().size(), std::size_t(9));
  Json many = parse(fc::call("geo.route", R"({"samples":1e30,"airports":[{"icao":"A","lat":0,"lon":0},{"icao":"B","lat":0,"lon":10}]})"));
  CHECK_EQ(many.at("result").at("arcs").at(std::size_t(0)).items().size(), std::size_t(257));
}

TEST(geo_intermediate_point_has_no_nan_at_the_poles_of_the_problem) {
  fc::LatLon a{10, 20};
  CHECK(std::isfinite(fc::intermediatePoint(a, a, 0.5).lat));
  fc::LatLon anti{-10, -160};
  fc::LatLon p = fc::intermediatePoint(a, anti, 0.5);
  CHECK(std::isfinite(p.lat) && std::isfinite(p.lon));
  CHECK(fc::isAntipodal(a, anti));
  CHECK(!fc::isAntipodal(a, {10, 21}));
}

TEST(weather_api_ops_round_trip) {
  Json m = parse(fc::call("metar.decode", R"({"raw":"KABE 181451Z 00000KT 10SM CLR 09/M01 A3062"})"));
  CHECK(m.at("ok").asBool());
  CHECK_EQ(m.at("result").at("category").asString(), std::string("VFR"));
  Json t = parse(fc::call("taf.decode", std::string(R"({"raw":")") + kSampleTaf + R"("})"));
  CHECK(t.at("ok").asBool());
  CHECK_EQ(t.at("result").at("periods").items().size(), std::size_t(6));
  Json w = parse(fc::call("wind.components", R"({"dir":360,"speed":10,"runway":"27"})"));
  CHECK_EQ(w.at("result").at("crossText").asString(), std::string("Crosswind 10 kt from the right"));
  Json g = parse(fc::call("geo.route", R"({"airports":[{"icao":"A","lat":0,"lon":0},{"icao":"B","lat":0,"lon":10}]})"));
  CHECK(g.at("ok").asBool());
  CHECK(!parse(fc::call("geo.route", R"({"airports":[]})")).at("ok").asBool());
}
