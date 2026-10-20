#include <cstdio>

#include "test.hpp"

int main() {
  int ran = 0;
  for (const auto& c : fctest::registry()) {
    int before = fctest::failures();
    std::printf("[ RUN  ] %s\n", c.name);
    c.fn();
    std::printf("[ %s ] %s\n", fctest::failures() == before ? "  OK" : "FAIL", c.name);
    ++ran;
  }
  std::printf("\n%d tests, %d failed checks\n", ran, fctest::failures());
  return fctest::failures() == 0 ? 0 : 1;
}
