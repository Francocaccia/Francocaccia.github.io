// Great-circle geometry and the route map.
#pragma once

#include "fc/json.hpp"

namespace fc {

struct LatLon {
  double lat = 0;  // degrees, north positive
  double lon = 0;  // degrees, east positive
};

constexpr double kEarthRadiusNm = 3440.065;  // mean radius 6371.0088 km

// Haversine distance in nautical miles.
double distanceNm(LatLon a, LatLon b);

// Initial true course from a to b, degrees 0..360.
double initialCourse(LatLon a, LatLon b);

// Point a fraction f (0..1) of the way along the great circle from a to b.
LatLon intermediatePoint(LatLon a, LatLon b, double f);

// Input: {airports:[{icao,name,lat,lon,role}], width?, height?, idPrefix?}.
// The route runs through the airports in order. Output: {svg, legs, totalNm}.
// legs: [{from, to, nm, km, course}].
Json routeMap(const Json& input);

}  // namespace fc
