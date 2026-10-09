#ifndef CPP_GREETER_H_
#define CPP_GREETER_H_

#include <string>

namespace greeter {

// Returns a greeting for `name`, e.g. "Hello, Bazel!".
std::string Greet(const std::string& name);

}  // namespace greeter

#endif  // CPP_GREETER_H_
