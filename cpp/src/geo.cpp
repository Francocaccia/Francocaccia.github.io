#include "fc/geo.hpp"

#include <algorithm>
#include <cmath>

#include "clamp.hpp"

namespace fc {

namespace {

constexpr double kPi = 3.14159265358979323846;
double rad(double d) { return d * kPi / 180.0; }
double deg(double r) { return r * 180.0 / kPi; }

struct Vec {
  double x, y, z;
};

Vec toVec(LatLon p) { return {std::cos(rad(p.lat)) * std::cos(rad(p.lon)), std::cos(rad(p.lat)) * std::sin(rad(p.lon)), std::sin(rad(p.lat))}; }

LatLon fromVec(Vec v) {
  double n = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
  return {deg(std::asin(std::max(-1.0, std::min(1.0, v.z / n)))), deg(std::atan2(v.y, v.x))};
}

double round3(double v) { return std::round(v * 1000.0) / 1000.0; }

}  // namespace

double distanceNm(LatLon a, LatLon b) {
  double dphi = rad(b.lat - a.lat), dl = rad(b.lon - a.lon);
  double h = std::sin(dphi / 2) * std::sin(dphi / 2) +
             std::cos(rad(a.lat)) * std::cos(rad(b.lat)) * std::sin(dl / 2) * std::sin(dl / 2);
  return 2.0 * kEarthRadiusNm * std::asin(std::min(1.0, std::sqrt(h)));
}

double initialCourse(LatLon a, LatLon b) {
  double dl = rad(b.lon - a.lon);
  double y = std::sin(dl) * std::cos(rad(b.lat));
  double x = std::cos(rad(a.lat)) * std::sin(rad(b.lat)) - std::sin(rad(a.lat)) * std::cos(rad(b.lat)) * std::cos(dl);
  double brg = deg(std::atan2(y, x));
  return std::fmod(brg + 360.0, 360.0);
}

LatLon intermediatePoint(LatLon a, LatLon b, double f) {
  double d = distanceNm(a, b) / kEarthRadiusNm;  // angular distance
  // Identical points have no direction; antipodal points have infinitely many great circles. Both
  // would divide by sin(d) = 0, so they return the start instead of NaN.
  if (d < 1e-12 || std::sin(d) < 1e-9) return a;
  double A = std::sin((1 - f) * d) / std::sin(d), B = std::sin(f * d) / std::sin(d);
  Vec va = toVec(a), vb = toVec(b);
  return fromVec({A * va.x + B * vb.x, A * va.y + B * vb.y, A * va.z + B * vb.z});
}

bool isAntipodal(LatLon a, LatLon b) { return distanceNm(a, b) / kEarthRadiusNm > kPi - 1e-6; }

Json routeGeometry(const Json& in) {
  struct Apt {
    std::string icao;
    LatLon pos;
  };
  std::vector<Apt> apts;
  for (const Json& a : in.at("airports").items()) {
    apts.push_back({a.at("icao").asString(), {a.at("lat").asNumber(), a.at("lon").asNumber()}});
  }
  const int steps = static_cast<int>(clampNumber(in.at("samples").asNumber(64), 8, 256, 64));

  Json legs = Json::array();
  Json arcs = Json::array();
  double total = 0;
  for (std::size_t i = 0; i + 1 < apts.size(); ++i) {
    double nm = distanceNm(apts[i].pos, apts[i + 1].pos);
    double course = initialCourse(apts[i].pos, apts[i + 1].pos);
    total += nm;
    Json leg = Json::object();
    leg.set("from", apts[i].icao).set("to", apts[i + 1].icao);
    leg.set("nm", std::round(nm * 10) / 10).set("km", std::round(nm * 1.852 * 10) / 10);
    leg.set("course", std::round(course * 10) / 10);
    legs.push(std::move(leg));

    // Sample points along the great circle, as [lat, lon] pairs. The ends are the airports themselves.
    Json arc = Json::array();
    for (int s = 0; s <= steps; ++s) {
      LatLon p = s == 0 ? apts[i].pos : s == steps ? apts[i + 1].pos : intermediatePoint(apts[i].pos, apts[i + 1].pos, static_cast<double>(s) / steps);
      Json pair = Json::array();
      pair.push(round3(p.lat));
      pair.push(round3(p.lon));
      arc.push(std::move(pair));
    }
    arcs.push(std::move(arc));
  }

  // The first view is centered halfway between the two ends of the route, so both sit about the same
  // distance from the middle of the globe.
  LatLon center = apts.size() >= 2 ? intermediatePoint(apts.front().pos, apts.back().pos, 0.5) : (apts.empty() ? LatLon{0, 0} : apts.front().pos);
  Json view = Json::object();
  view.set("lat", std::round(center.lat * 100) / 100).set("lon", std::round(center.lon * 100) / 100);

  Json out = Json::object();
  out.set("legs", std::move(legs));
  out.set("totalNm", std::round(total * 10) / 10);
  out.set("view", std::move(view));
  out.set("arcs", std::move(arcs));
  return out;
}

}  // namespace fc
