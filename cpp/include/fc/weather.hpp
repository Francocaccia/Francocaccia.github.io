// Aviation weather: METAR and TAF decoders and the runway wind calculator.
// Everything returned as markup is built from escaped text, so it is safe to
// assign to innerHTML even when the input was pasted by a visitor.
#pragma once

#include <string>

#include "fc/json.hpp"

namespace fc {

// Decodes one METAR or SPECI. Returns on success:
//   {ok:true, station, category, categoryWhy, html, groups, rows, warnings,
//    wind:{dir, speedKt, gustKt, variable, calm}, tempC, dewC, humidity, cloudBaseFt, hpa, inHg}
// and on failure {ok:false, error}. `observedAt` is an optional ISO time shown in the table.
Json decodeMetar(const std::string& raw, const std::string& observedAt = "");

// Decodes one TAF. Input: {raw, monthDays?, width?, idPrefix?}.
// Returns {ok, station, validity, periods, html, timeline} or {ok:false, error}.
Json decodeTaf(const Json& input);

// Headwind and crosswind for a runway. Input: {dir (true), speed (kt), gust?, runway ("27L"),
// variation? (degrees, east positive), idPrefix?}. Returns {headwind, crosswind, ..., svg} or {error}.
Json windComponents(const Json& input);

}  // namespace fc
