#include "cpp/greeter.h"

namespace greeter {

std::string Greet(const std::string& name) {
  return "Hello, " + (name.empty() ? std::string("world") : name) + "!";
}

}  // namespace greeter
