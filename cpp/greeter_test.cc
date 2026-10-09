// Minimal dependency-free test: cc_test passes when main() returns 0.
#include <iostream>
#include <string>

#include "cpp/greeter.h"

namespace {

int failures = 0;

void ExpectEq(const std::string& actual, const std::string& expected) {
  if (actual != expected) {
    std::cerr << "FAIL: expected \"" << expected << "\", got \"" << actual
              << "\"\n";
    ++failures;
  }
}

}  // namespace

int main() {
  ExpectEq(greeter::Greet("Bazel"), "Hello, Bazel!");
  ExpectEq(greeter::Greet(""), "Hello, world!");

  if (failures == 0) std::cout << "All greeter tests passed\n";
  return failures == 0 ? 0 : 1;
}
