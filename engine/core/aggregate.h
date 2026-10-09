#ifndef ENGINE_CORE_AGGREGATE_H_
#define ENGINE_CORE_AGGREGATE_H_

#include <optional>
#include <string>

#include "engine/core/status.h"
#include "engine/core/table.h"

namespace analytics::engine {

enum class AggregateFunction { kSum, kAvg, kCount, kMin, kMax };

struct Aggregation {
  AggregateFunction function;
  // Input column. Empty is allowed only for kCount (counts rows).
  std::string column;
};

// Result column name: "<function>(<column>)" in lower case, "count(*)" for an
// empty column.
std::string AggregateColumnName(const Aggregation& aggregation);

// Runs one aggregation over `table`, optionally grouped by one column.
//
// Result layout: the group column first (same name and type), then the
// aggregate column. Types: count -> int64; avg -> double; sum/min/max keep the
// input type. Rows are ascending by group key (byte order for strings; for
// doubles NaN sorts last). Without group_by the result has exactly one row.
//
// Semantics (decided here, see docs/proto-compatibility.md):
//   * sum/avg need int64 or double input; count/min/max accept any type.
//   * int64 sum wraps on overflow (two's complement), never undefined behavior.
//   * avg of int64 is a double.
//   * empty input (no rows, ungrouped): count = 0, sum = 0; avg, min and max
//     have no value (the wire has no NULL) and are kInvalidAggregation.
//     Grouped on an empty table gives zero rows, not an error.
//   * double min/max: NaN is ignored unless every value is NaN. NaN in sum/avg
//     propagates as IEEE-754 says.
// Errors: kUnknownColumn, kInvalidAggregation (see above, or a missing column
// for a function other than count).
Result<Table> Aggregate(const Table& table, const Aggregation& aggregation,
                        const std::optional<std::string>& group_by = std::nullopt);

}  // namespace analytics::engine

#endif  // ENGINE_CORE_AGGREGATE_H_
