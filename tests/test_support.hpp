#ifndef ZOAL_MP_TESTS_TEST_SUPPORT_HPP
#define ZOAL_MP_TESTS_TEST_SUPPORT_HPP

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace test {

inline void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    std::exit(1);
  }
}

inline void near(double got, double want, double tolerance,
                 const std::string &message) {
  if (!(std::fabs(got - want) <= tolerance)) {
    std::cerr << "FAIL: " << message << " (got " << got << ", want " << want
              << " +/- " << tolerance << ")\n";
    std::exit(1);
  }
}

} // namespace test

#endif // ZOAL_MP_TESTS_TEST_SUPPORT_HPP
