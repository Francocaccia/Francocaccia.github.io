// Turns the bot's public /stats payload into KPI tiles.
// Only four percentage fields plus the day count and dates are ever read, so
// nothing else the endpoint returns (counts, allocation, ids) can reach the page.
#pragma once

#include <string>

#include "fc/json.hpp"

namespace fc {

// Returns {tiles:[{id,label,value,text,tone,caption}], daysTracked, startDate, asOf, note, hasData}.
// `trackingSince` is used for captions while a metric is still null.
Json normalizeStats(const Json& raw, const std::string& trackingSince);

}  // namespace fc
