#include "engine/core/table.h"

#include <iterator>
#include <set>
#include <utility>

namespace analytics::engine {

ColumnType TypeOf(const ColumnValues& values) {
  switch (values.index()) {
    case 0: return ColumnType::kInt64;
    case 1: return ColumnType::kDouble;
    default: return ColumnType::kString;
  }
}

size_t SizeOf(const ColumnValues& values) {
  return std::visit([](const auto& v) { return v.size(); }, values);
}

const char* TypeName(ColumnType type) {
  switch (type) {
    case ColumnType::kInt64: return "int64";
    case ColumnType::kDouble: return "double";
    case ColumnType::kString: return "string";
  }
  return "unknown";
}

Column Column::Empty(const ColumnSpec& spec) {
  switch (spec.type) {
    case ColumnType::kInt64: return Column(spec.name, std::vector<int64_t>{});
    case ColumnType::kDouble: return Column(spec.name, std::vector<double>{});
    case ColumnType::kString: return Column(spec.name, std::vector<std::string>{});
  }
  return Column(spec.name, std::vector<int64_t>{});
}

namespace {

std::optional<Error> ValidateNames(const std::vector<Column>& columns) {
  std::set<std::string> seen;
  for (const Column& c : columns) {
    if (c.name().empty()) {
      return Error{ErrorCode::kMalformedRequest, "column name must not be empty"};
    }
    if (!seen.insert(c.name()).second) {
      return Error{ErrorCode::kMalformedRequest, "duplicate column name '" + c.name() + "'"};
    }
  }
  return std::nullopt;
}

}  // namespace

Result<Table> Table::Create(const std::vector<ColumnSpec>& schema) {
  std::vector<Column> columns;
  columns.reserve(schema.size());
  for (const ColumnSpec& spec : schema) columns.push_back(Column::Empty(spec));
  return FromColumns(std::move(columns));
}

Result<Table> Table::FromColumns(std::vector<Column> columns) {
  if (auto err = ValidateNames(columns)) return *err;
  Table table;
  if (!columns.empty()) {
    table.row_count_ = columns.front().size();
    for (const Column& c : columns) {
      if (c.size() != table.row_count_) {
        return Error{ErrorCode::kSchemaMismatch,
                     "column '" + c.name() + "' has " + std::to_string(c.size()) +
                         " values, expected " + std::to_string(table.row_count_)};
      }
    }
  }
  table.columns_ = std::move(columns);
  return table;
}

std::vector<ColumnSpec> Table::schema() const {
  std::vector<ColumnSpec> out;
  out.reserve(columns_.size());
  for (const Column& c : columns_) out.push_back({c.name(), c.type()});
  return out;
}

Result<const Column*> Table::FindColumn(const std::string& name) const {
  for (const Column& c : columns_) {
    if (c.name() == name) return &c;
  }
  return Error{ErrorCode::kUnknownColumn, "unknown column '" + name + "'"};
}

std::optional<Error> Table::AppendColumns(std::vector<ColumnValues> batch) {
  if (batch.size() != columns_.size()) {
    return Error{ErrorCode::kSchemaMismatch,
                 "batch has " + std::to_string(batch.size()) + " columns, schema has " +
                     std::to_string(columns_.size())};
  }
  size_t rows = batch.empty() ? 0 : SizeOf(batch.front());
  for (size_t i = 0; i < batch.size(); ++i) {
    if (TypeOf(batch[i]) != columns_[i].type()) {
      return Error{ErrorCode::kSchemaMismatch,
                   "column '" + columns_[i].name() + "' expects " +
                       TypeName(columns_[i].type()) + " values, got " +
                       TypeName(TypeOf(batch[i]))};
    }
    if (SizeOf(batch[i]) != rows) {
      return Error{ErrorCode::kSchemaMismatch,
                   "column '" + columns_[i].name() + "' has " +
                       std::to_string(SizeOf(batch[i])) + " values, expected " +
                       std::to_string(rows)};
    }
  }
  // Validated: from here on nothing fails (bad_alloc aside), so all-or-nothing holds.
  for (size_t i = 0; i < batch.size(); ++i) {
    std::visit(
        [&](auto& dst) {
          auto& src = std::get<std::decay_t<decltype(dst)>>(batch[i]);
          dst.insert(dst.end(), std::make_move_iterator(src.begin()),
                     std::make_move_iterator(src.end()));
        },
        columns_[i].values_);
  }
  row_count_ += rows;
  return std::nullopt;
}

}  // namespace analytics::engine
