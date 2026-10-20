#include "fc/geo.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "clamp.hpp"

namespace fc {

namespace {

constexpr double kPi = 3.14159265358979323846;
double rad(double d) { return d * kPi / 180.0; }
double deg(double r) { return r * 180.0 / kPi; }

std::string num(double v, int decimals = 1) {
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
  return buf;
}

std::string escape(const std::string& s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += c;
    }
  }
  return out;
}

struct Vec {
  double x, y, z;
};

Vec toVec(LatLon p) { return {std::cos(rad(p.lat)) * std::cos(rad(p.lon)), std::cos(rad(p.lat)) * std::sin(rad(p.lon)), std::sin(rad(p.lat))}; }

LatLon fromVec(Vec v) {
  double n = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
  return {deg(std::asin(v.z / n)), deg(std::atan2(v.y, v.x))};
}

// Orthographic projection centered on (lat0, lon0). Returns false for the far side of the globe.
struct Ortho {
  double lat0, lon0, cx, cy, R;
  bool project(LatLon p, double& x, double& y) const {
    double dl = rad(p.lon - lon0), phi = rad(p.lat), p0 = rad(lat0);
    double cosc = std::sin(p0) * std::sin(phi) + std::cos(p0) * std::cos(phi) * std::cos(dl);
    if (cosc < 0.0) return false;
    x = cx + R * std::cos(phi) * std::sin(dl);
    y = cy - R * (std::cos(p0) * std::sin(phi) - std::sin(p0) * std::cos(phi) * std::cos(dl));
    return true;
  }
};

// Adds a sampled line to `path`, starting a new subpath wherever it crosses the horizon.
void addPolyline(std::string& path, const Ortho& o, const std::vector<LatLon>& pts) {
  bool pen = false;
  for (const LatLon& p : pts) {
    double x, y;
    if (!o.project(p, x, y)) {
      pen = false;
      continue;
    }
    path += (pen ? " L" : " M") + num(x) + " " + num(y);
    pen = true;
  }
}

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
  if (d < 1e-12) return a;
  double A = std::sin((1 - f) * d) / std::sin(d), B = std::sin(f * d) / std::sin(d);
  Vec va = toVec(a), vb = toVec(b);
  return fromVec({A * va.x + B * vb.x, A * va.y + B * vb.y, A * va.z + B * vb.z});
}

Json routeMap(const Json& in) {
  struct Apt {
    std::string icao, name;
    LatLon pos;
  };
  std::vector<Apt> apts;
  for (const Json& a : in.at("airports").items()) {
    apts.push_back({a.at("icao").asString(), a.at("name").asString(), {a.at("lat").asNumber(), a.at("lon").asNumber()}});
  }

  int W = static_cast<int>(clampNumber(in.at("width").asNumber(560), 160, 1200, 560));
  int H = static_cast<int>(clampNumber(in.at("height").asNumber(W), 160, 1200, W));
  std::string prefix = "fca";
  if (in.at("idPrefix").isString()) {
    std::string id;
    for (char c : in.at("idPrefix").asString()) {
      if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') id += c;
    }
    if (!id.empty()) prefix = id;
  }

  // Center the globe on the mean position of the route.
  Vec sum{0, 0, 0};
  for (const Apt& a : apts) {
    Vec v = toVec(a.pos);
    sum = {sum.x + v.x, sum.y + v.y, sum.z + v.z};
  }
  LatLon center = apts.empty() ? LatLon{0, 0} : fromVec(sum);
  double R = 0.46 * std::min(W, H);
  Ortho o{center.lat, center.lon, W / 2.0, H / 2.0, R};
  const double fs = W < 420 ? 11.0 : 12.0;  // label size in px at the rendered width

  std::string svg = "<svg class=\"fca-svg\" xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " + std::to_string(W) + " " +
                    std::to_string(H) + "\" role=\"img\" aria-labelledby=\"" + prefix + "-t " + prefix + "-d\"><title id=\"" + prefix +
                    "-t\">Route map</title>";

  // Legs
  Json legs = Json::array();
  double total = 0;
  std::string summary;
  for (std::size_t i = 0; i + 1 < apts.size(); ++i) {
    double nm = distanceNm(apts[i].pos, apts[i + 1].pos);
    double course = initialCourse(apts[i].pos, apts[i + 1].pos);
    total += nm;
    Json leg = Json::object();
    leg.set("from", apts[i].icao).set("to", apts[i + 1].icao);
    leg.set("nm", std::round(nm * 10) / 10).set("km", std::round(nm * 1.852 * 10) / 10);
    leg.set("course", std::round(course * 10) / 10);
    legs.push(std::move(leg));
    summary += apts[i].icao + " to " + apts[i + 1].icao + " " + num(nm, 0) + " nm, course " + num(course, 0) + " degrees true. ";
  }
  svg += "<desc id=\"" + prefix + "-d\">Orthographic globe with graticule every 15 degrees. Great-circle route through " +
         std::to_string(apts.size()) + " airports. " + escape(summary) + "Coordinates are approximate.</desc>";

  // Globe disc and graticule
  svg += "<circle class=\"fca-disc\" cx=\"" + num(W / 2.0) + "\" cy=\"" + num(H / 2.0) + "\" r=\"" + num(R) + "\"/>";
  std::string minor, major;
  for (int lon = -180; lon < 180; lon += 15) {
    std::vector<LatLon> line;
    for (double lat = -90; lat <= 90; lat += 2) line.push_back({lat, static_cast<double>(lon)});
    addPolyline(minor, o, line);
  }
  for (int lat = -75; lat <= 75; lat += 15) {
    std::vector<LatLon> line;
    for (double lon = -180; lon <= 180; lon += 2) line.push_back({static_cast<double>(lat), lon});
    addPolyline(lat == 0 ? major : minor, o, line);
  }
  svg += "<path class=\"fca-grid\" d=\"" + minor + "\"/><path class=\"fca-equator\" d=\"" + major + "\"/>";

  // Parallel labels along the central meridian
  for (int lat = -60; lat <= 60; lat += 30) {
    double x, y;
    if (o.project({static_cast<double>(lat), center.lon}, x, y)) {
      std::string label = lat == 0 ? "0" : std::to_string(std::abs(lat)) + (lat > 0 ? "N" : "S");
      svg += "<text class=\"fca-tick\" x=\"" + num(x + 4) + "\" y=\"" + num(y - 3) + "\" font-size=\"" + num(fs - 1) + "\">" + label + "</text>";
    }
  }

  // Great-circle arcs, with a direction arrow at each midpoint
  for (std::size_t i = 0; i + 1 < apts.size(); ++i) {
    std::vector<LatLon> pts;
    const int steps = 64;
    for (int s = 0; s <= steps; ++s) pts.push_back(intermediatePoint(apts[i].pos, apts[i + 1].pos, static_cast<double>(s) / steps));
    std::string d;
    addPolyline(d, o, pts);
    svg += "<path class=\"fca-arc\" d=\"" + d + "\"/>";
    double x1, y1, x2, y2;
    if (o.project(pts[steps / 2 - 1], x1, y1) && o.project(pts[steps / 2 + 1], x2, y2)) {
      double ang = deg(std::atan2(y2 - y1, x2 - x1));
      LatLon mid = pts[steps / 2];
      double mx, my;
      if (o.project(mid, mx, my)) {
        svg += "<path class=\"fca-arrow\" d=\"M-6 -4 L5 0 L-6 4 Z\" transform=\"translate(" + num(mx) + " " + num(my) + ") rotate(" +
               num(ang) + ")\"/>";
      }
    }
  }

  // Airport markers and labels, pushed away from the globe center so they stay clear of the route
  struct Placed {
    double x, y;
    bool right;
  };
  std::vector<Placed> placed;
  for (const Apt& a : apts) {
    double x, y;
    if (!o.project(a.pos, x, y)) continue;
    double dx = x - W / 2.0, dy = y - H / 2.0;
    bool right = dx >= 0;
    // Two airports close together put their labels on opposite sides.
    for (const Placed& p : placed) {
      if (std::hypot(x - p.x, y - p.y) < 44) right = !p.right;
    }
    placed.push_back({x, y, right});
    svg += "<circle class=\"fca-apt\" cx=\"" + num(x) + "\" cy=\"" + num(y) + "\" r=\"5\"/><text class=\"fca-label\" x=\"" +
           num(x + (right ? 10 : -10)) + "\" y=\"" + num(y + (dy < 0 ? -8 : 16)) + "\" font-size=\"" + num(fs + 1) +
           "\" text-anchor=\"" + (right ? "start" : "end") + "\">" + escape(a.icao) + "</text>";
  }
  svg += "<text class=\"fca-note\" x=\"" + num(W / 2.0) + "\" y=\"" + std::to_string(H - 4) + "\" font-size=\"" + num(fs - 1) +
         "\" text-anchor=\"middle\">Coordinates approximate</text></svg>";

  Json out = Json::object();
  out.set("svg", svg);
  out.set("legs", std::move(legs));
  out.set("totalNm", std::round(total * 10) / 10);
  return out;
}

}  // namespace fc
