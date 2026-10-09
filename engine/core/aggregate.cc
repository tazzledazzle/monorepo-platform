#include "engine/core/aggregate.h"

#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace analytics::engine {
namespace {

const char* FunctionName(AggregateFunction f) {
  switch (f) {
    case AggregateFunction::kSum: return "sum";
    case AggregateFunction::kAvg: return "avg";
    case AggregateFunction::kCount: return "count";
    case AggregateFunction::kMin: return "min";
    case AggregateFunction::kMax: return "max";
  }
  return "unknown";
}

// Total order used for grouping and min/max: ascending, NaN last.
struct KeyLess {
  template <typename T>
  bool operator()(const T& a, const T& b) const {
    if constexpr (std::is_floating_point_v<T>) {
      if (std::isnan(a)) return false;
      if (std::isnan(b)) return true;
    }
    return a < b;
  }
};

// Assigns every row a dense group id; ids follow ascending key order.
// `keys_out` receives one key per group.
template <typename T>
size_t AssignGroups(const std::vector<T>& keys, std::vector<size_t>* group_of_row,
                    std::vector<T>* keys_out) {
  std::map<T, size_t, KeyLess> index;
  for (const T& k : keys) index.emplace(k, 0);
  size_t next = 0;
  for (auto& [k, id] : index) {
    id = next++;
    keys_out->push_back(k);
  }
  group_of_row->reserve(keys.size());
  for (const T& k : keys) group_of_row->push_back(index.find(k)->second);
  return next;
}

Result<Column> GroupKeys(const Column& key_column, std::vector<size_t>* group_of_row,
                         size_t* num_groups) {
  return std::visit(
      [&](const auto& keys) -> Result<Column> {
        using V = std::decay_t<decltype(keys)>;
        V out;
        *num_groups = AssignGroups(keys, group_of_row, &out);
        return Column(key_column.name(), std::move(out));
      },
      key_column.values());
}

// Number of rows per group.
std::vector<int64_t> CountPerGroup(const std::vector<size_t>& group_of_row, size_t groups) {
  std::vector<int64_t> counts(groups, 0);
  for (size_t g : group_of_row) ++counts[g];
  return counts;
}

template <typename T>
Result<ColumnValues> MinMax(const std::vector<T>& values, const std::vector<size_t>& group_of_row,
                            const std::vector<int64_t>& counts, bool want_max,
                            const std::string& what) {
  const size_t groups = counts.size();
  std::vector<T> best(groups);
  std::vector<bool> seen(groups, false);
  for (size_t i = 0; i < values.size(); ++i) {
    const size_t g = group_of_row[i];
    const T& v = values[i];
    if (!seen[g]) {
      best[g] = v;
      seen[g] = true;
      continue;
    }
    KeyLess less;
    if constexpr (std::is_floating_point_v<T>) {
      // NaN is ignored unless it is all there is.
      if (std::isnan(v)) continue;
      if (std::isnan(best[g])) { best[g] = v; continue; }
    }
    if (want_max ? less(best[g], v) : less(v, best[g])) best[g] = v;
  }
  for (size_t g = 0; g < groups; ++g) {
    if (!seen[g]) {
      return Error{ErrorCode::kInvalidAggregation, what + " over zero rows has no value"};
    }
  }
  return ColumnValues(std::move(best));
}

}  // namespace

std::string AggregateColumnName(const Aggregation& aggregation) {
  return std::string(FunctionName(aggregation.function)) + "(" +
         (aggregation.column.empty() ? "*" : aggregation.column) + ")";
}

Result<Table> Aggregate(const Table& table, const Aggregation& aggregation,
                        const std::optional<std::string>& group_by) {
  const std::string out_name = AggregateColumnName(aggregation);
  const bool is_count = aggregation.function == AggregateFunction::kCount;

  // Validate everything before doing work.
  const Column* input = nullptr;
  if (aggregation.column.empty()) {
    if (!is_count) {
      return Error{ErrorCode::kInvalidAggregation,
                   std::string(FunctionName(aggregation.function)) + " requires a column"};
    }
  } else {
    auto found = table.FindColumn(aggregation.column);
    if (!found.ok()) return found.error();
    input = found.value();
    const bool numeric = input->type() != ColumnType::kString;
    if (!numeric && (aggregation.function == AggregateFunction::kSum ||
                     aggregation.function == AggregateFunction::kAvg)) {
      return Error{ErrorCode::kInvalidAggregation,
                   "cannot " + std::string(FunctionName(aggregation.function)) +
                       " string column '" + input->name() + "'"};
    }
  }
  const Column* key = nullptr;
  if (group_by.has_value()) {
    auto found = table.FindColumn(*group_by);
    if (!found.ok()) return found.error();
    key = found.value();
  }

  // Group assignment: no group_by is one group holding every row.
  std::vector<size_t> group_of_row;
  size_t groups = 1;
  std::vector<Column> out_columns;
  if (key != nullptr) {
    auto keys = GroupKeys(*key, &group_of_row, &groups);
    out_columns.push_back(std::move(keys).value());
  } else {
    group_of_row.assign(table.row_count(), 0);
  }
  const std::vector<int64_t> counts = CountPerGroup(group_of_row, groups);

  ColumnValues result;
  if (is_count) {
    if (input != nullptr && input->size() != group_of_row.size()) {
      return Error{ErrorCode::kMalformedRequest, "column length differs from row count"};
    }
    result = counts;  // no NULLs, so count(col) == count(*)
  } else {
    const std::string what = std::string(FunctionName(aggregation.function)) + "(" +
                             aggregation.column + ")";
    switch (aggregation.function) {
      case AggregateFunction::kSum:
      case AggregateFunction::kAvg: {
        const bool avg = aggregation.function == AggregateFunction::kAvg;
        if (avg) {
          for (int64_t c : counts) {
            if (c == 0) {
              return Error{ErrorCode::kInvalidAggregation, what + " over zero rows has no value"};
            }
          }
        }
        std::visit(
            [&](const auto& values) {
              using T = typename std::decay_t<decltype(values)>::value_type;
              if constexpr (std::is_same_v<T, int64_t>) {
                std::vector<uint64_t> sums(groups, 0);  // unsigned: wraps, no UB
                for (size_t i = 0; i < values.size(); ++i) {
                  sums[group_of_row[i]] += static_cast<uint64_t>(values[i]);
                }
                if (avg) {
                  std::vector<double> out(groups);
                  // Sum in double so large inputs do not wrap before dividing.
                  std::vector<double> dsums(groups, 0.0);
                  for (size_t i = 0; i < values.size(); ++i) {
                    dsums[group_of_row[i]] += static_cast<double>(values[i]);
                  }
                  for (size_t g = 0; g < groups; ++g) {
                    out[g] = dsums[g] / static_cast<double>(counts[g]);
                  }
                  result = std::move(out);
                } else {
                  std::vector<int64_t> out(groups);
                  for (size_t g = 0; g < groups; ++g) out[g] = static_cast<int64_t>(sums[g]);
                  result = std::move(out);
                }
              } else if constexpr (std::is_same_v<T, double>) {
                std::vector<double> sums(groups, 0.0);
                for (size_t i = 0; i < values.size(); ++i) sums[group_of_row[i]] += values[i];
                if (avg) {
                  for (size_t g = 0; g < groups; ++g) sums[g] /= static_cast<double>(counts[g]);
                }
                result = std::move(sums);
              }
            },
            input->values());
        break;
      }
      case AggregateFunction::kMin:
      case AggregateFunction::kMax: {
        const bool want_max = aggregation.function == AggregateFunction::kMax;
        Result<ColumnValues> r = std::visit(
            [&](const auto& values) {
              return MinMax(values, group_of_row, counts, want_max, what);
            },
            input->values());
        if (!r.ok()) return r.error();
        result = std::move(r).value();
        break;
      }
      case AggregateFunction::kCount:
        break;  // handled above
    }
  }
  out_columns.emplace_back(out_name, std::move(result));
  return Table::FromColumns(std::move(out_columns));
}

}  // namespace analytics::engine
