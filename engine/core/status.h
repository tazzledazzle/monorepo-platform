#ifndef ENGINE_CORE_STATUS_H_
#define ENGINE_CORE_STATUS_H_

#include <string>
#include <utility>
#include <variant>

namespace analytics::engine {

// Why an engine operation failed. Each value maps to gRPC INVALID_ARGUMENT in
// engine/service (docs/proto-compatibility.md); the finer reason becomes the
// optional ErrorDetail. engine/core never throws across its API.
enum class ErrorCode {
  kUnknownColumn,       // column (aggregation input or group_by) not in the table
  kInvalidAggregation,  // unsupported function/type combination, or no input rows
  kSchemaMismatch,      // data does not match the table schema
  kMalformedRequest,    // structurally invalid input (empty/duplicate names, ...)
};

struct Error {
  ErrorCode code;
  std::string message;  // one line, human readable
};

// Value-or-error. Deliberately small: C++20 has no std::expected.
template <typename T>
class Result {
 public:
  Result(T value) : state_(std::move(value)) {}  // NOLINT: implicit by design
  Result(Error error) : state_(std::move(error)) {}  // NOLINT

  bool ok() const { return std::holds_alternative<T>(state_); }
  const T& value() const& { return std::get<T>(state_); }
  T& value() & { return std::get<T>(state_); }
  T&& value() && { return std::get<T>(std::move(state_)); }
  const Error& error() const { return std::get<Error>(state_); }

 private:
  std::variant<T, Error> state_;
};

}  // namespace analytics::engine

#endif  // ENGINE_CORE_STATUS_H_
