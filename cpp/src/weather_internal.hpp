// Shared pieces of the METAR and TAF decoders (not part of the public API).
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace fc::wx {

constexpr double kKtPerMs = 1.9438445;
constexpr double kMPerSm = 1609.344;

// ------------------------------------------------------------------ text helpers
std::string esc(const std::string& s);
std::string fmt(double v, int decimals = 0);
std::string thousands(long v);
std::string upperOf(std::string s);
std::string cap(std::string s);
std::string two(int v);
std::vector<std::string> words(const std::string& s);
bool digitsAt(const std::string& s, std::size_t pos, std::size_t n);
int intAt(const std::string& s, std::size_t pos, std::size_t n);
const char* compass(int degrees);

// -------------------------------------------------------------------- elements
struct Wind {
  bool present = false, calm = false, variable = false, missing = false;
  int dir = -1, speed = 0, gust = -1;
  std::string unit = "KT";
  int varFrom = -1, varTo = -1;
};

struct Vis {
  bool set = false, more = false, less = false;
  double meters = 0;
  std::string text;
};

struct Cloud {
  std::string cover;  // FEW SCT BKN OVC VV
  int hundreds = -1;  // height in hundreds of feet AGL, -1 when missing
  std::string kind;   // CB, TCU or empty
};

struct Elements {
  Wind wind;
  Vis vis;
  bool cavok = false, nsw = false, noCloud = false;
  std::string noCloudText;
  std::vector<std::string> wx;  // decoded phrases
  std::vector<Cloud> clouds;
  std::vector<std::string> extra;  // runway visual range, wind shear
};

struct Group {
  std::string text, kind, tip;
};

struct Category {
  std::string name, why;  // VFR / MVFR / IFR / LIFR / Unknown
};

double toKt(double v, const std::string& unit);
int ktOf(const Wind& w, bool gust = false);
std::string windText(const Wind& w);
std::string cloudText(const Cloud& c);
int ceilingFt(const Elements& e);
Category categoryOf(const Elements& e);

// Tries to read one element group starting at tk[i]. On success fills `g`, sets `used`
// to the number of tokens consumed (visibility fractions span two) and returns true.
bool parseElement(const std::vector<std::string>& tk, std::size_t i, Elements& e, Group& g, std::size_t& used);

// A one-paragraph plain-English summary of a set of elements.
std::string describe(const Elements& e);

// <span> per group, colored by kind, with the meaning as a focusable tooltip.
std::string groupsHtml(const std::vector<Group>& groups);
std::string catBadge(const std::string& name);

}  // namespace fc::wx
