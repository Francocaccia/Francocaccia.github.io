// Safe double-to-integer conversion for numbers that arrive in JSON (not part of the public API).
#pragma once

#include <cmath>

namespace fc {

// NaN becomes the fallback; anything else is clamped into [lo, hi], so a cast of the result is always defined.
inline double clampNumber(double v, double lo, double hi, double fallback) {
  if (std::isnan(v)) v = fallback;
  return v < lo ? lo : (v > hi ? hi : v);
}

}  // namespace fc
