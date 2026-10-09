#ifndef ENGINE_CORE_TABLE_H_
#define ENGINE_CORE_TABLE_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "engine/core/status.h"

namespace analytics::engine {

enum class ColumnType { kInt64, kDouble, kString };

// Physical storage of one column: a typed vector. The variant index matches
// ColumnType (int64, double, string).
using ColumnValues =
    std::variant<std::vector<int64_t>, std::vector<double>, std::vector<std::string>>;

ColumnType TypeOf(const ColumnValues& values);
size_t SizeOf(const ColumnValues& values);
const char* TypeName(ColumnType type);

struct ColumnSpec {
  std::string name;
  ColumnType type;
  bool operator==(const ColumnSpec&) const = default;
};

// A named, typed column. No NULLs (v1 wire contract).
class Column {
 public:
  Column(std::string name, ColumnValues values)
      : name_(std::move(name)), values_(std::move(values)) {}
  static Column Empty(const ColumnSpec& spec);

  const std::string& name() const { return name_; }
  ColumnType type() const { return TypeOf(values_); }
  size_t size() const { return SizeOf(values_); }
  const ColumnValues& values() const { return values_; }

 private:
  friend class Table;
  std::string name_;
  ColumnValues values_;
};

// In-memory columnar table. Not internally synchronized: callers share a
// const Table across threads, or guard mutation themselves.
class Table {
 public:
  // Empty table (zero rows) with the given schema. Fails on an empty or
  // duplicate column name.
  static Result<Table> Create(const std::vector<ColumnSpec>& schema);
  // Table from complete columns. Fails on empty/duplicate names or ragged lengths.
  static Result<Table> FromColumns(std::vector<Column> columns);

  size_t row_count() const { return row_count_; }
  size_t column_count() const { return columns_.size(); }
  const std::vector<Column>& columns() const { return columns_; }
  std::vector<ColumnSpec> schema() const;

  // Unknown name yields kUnknownColumn. The pointer lives as long as the table
  // is not modified.
  Result<const Column*> FindColumn(const std::string& name) const;

  // Appends rows given as one vector per column, in schema order. All or
  // nothing: on error the table is unchanged. Errors: kSchemaMismatch (wrong
  // vector count, wrong vector type, ragged lengths).
  std::optional<Error> AppendColumns(std::vector<ColumnValues> batch);

 private:
  std::vector<Column> columns_;
  size_t row_count_ = 0;
};

}  // namespace analytics::engine

#endif  // ENGINE_CORE_TABLE_H_
