// Tiny test harness: TEST(name) { CHECK(cond); CHECK_EQ(a, b); }
#pragma once

#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace fctest {

struct Case {
  const char* name;
  std::function<void()> fn;
};

inline std::vector<Case>& registry() {
  static std::vector<Case> cases;
  return cases;
}

inline int& failures() {
  static int n = 0;
  return n;
}

struct Registrar {
  Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

template <typename T>
std::string show(const T& v);

template <typename T>
std::string show(const std::vector<T>& v) {
  std::string out = "[";
  for (std::size_t i = 0; i < v.size(); ++i) out += (i ? ", " : "") + show(v[i]);
  return out + "]";
}

template <typename T>
std::string show(const T& v) {
  std::ostringstream os;
  os << v;
  return os.str();
}

inline void fail(const char* file, int line, const std::string& what) {
  ++failures();
  std::printf("  FAIL %s:%d  %s\n", file, line, what.c_str());
}

}  // namespace fctest

#define TEST(name)                                                        \
  static void test_##name();                                              \
  static fctest::Registrar registrar_##name(#name, test_##name);          \
  static void test_##name()

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) fctest::fail(__FILE__, __LINE__, "CHECK(" #cond ")");    \
  } while (0)

#define CHECK_EQ(a, b)                                                    \
  do {                                                                    \
    auto va_ = (a);                                                       \
    auto vb_ = (b);                                                       \
    if (!(va_ == vb_))                                                    \
      fctest::fail(__FILE__, __LINE__,                                    \
                   "CHECK_EQ(" #a ", " #b ")  got " + fctest::show(va_) + \
                       " vs " + fctest::show(vb_));                       \
  } while (0)
