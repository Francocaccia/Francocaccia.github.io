// Great-circle geometry for the route. The globe itself is drawn in the browser (src/lib/globe.ts).
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

// Point a fraction f (0..1) of the way along the great circle from a to b. Identical or antipodal
// points have no single great circle; they return a.
LatLon intermediatePoint(LatLon a, LatLon b, double f);

// True when a and b are (nearly) opposite points on the sphere.
bool isAntipodal(LatLon a, LatLon b);

// Input: {airports:[{icao,lat,lon,...}], samples?}. The route runs through the airports in order.
// Output: {legs, totalNm, view, arcs}.
//   legs: [{from, to, nm, km, course}]  (haversine distance, initial true course)
//   view: {lat, lon}                    the midpoint of the first and last airport, for the first view
//   arcs: [[[lat, lon]...]...]          one sampled great circle per leg, ends on the airports
Json routeGeometry(const Json& input);

}  // namespace fc
