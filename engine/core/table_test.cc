#include "engine/core/table.h"

#include <gtest/gtest.h>

namespace analytics::engine {
namespace {

using Ints = std::vector<int64_t>;
using Doubles = std::vector<double>;
using Strings = std::vector<std::string>;

Table SalesSchema() {
  auto t = Table::Create({{"region", ColumnType::kString},
                          {"units", ColumnType::kInt64},
                          {"price", ColumnType::kDouble}});
  EXPECT_TRUE(t.ok());
  return std::move(t).value();
}

TEST(TableTest, CreateHasSchemaAndNoRows) {
  Table t = SalesSchema();
  EXPECT_EQ(t.row_count(), 0u);
  EXPECT_EQ(t.column_count(), 3u);
  std::vector<ColumnSpec> want = {{"region", ColumnType::kString},
                                  {"units", ColumnType::kInt64},
                                  {"price", ColumnType::kDouble}};
  EXPECT_EQ(t.schema(), want);
}

TEST(TableTest, ZeroColumnTableIsValid) {
  auto t = Table::Create({});
  ASSERT_TRUE(t.ok());
  EXPECT_EQ(t.value().row_count(), 0u);
  EXPECT_FALSE(t.value().AppendColumns({}).has_value());
}

TEST(TableTest, CreateRejectsEmptyAndDuplicateNames) {
  auto empty = Table::Create({{"", ColumnType::kInt64}});
  ASSERT_FALSE(empty.ok());
  EXPECT_EQ(empty.error().code, ErrorCode::kMalformedRequest);
  auto dup = Table::Create({{"a", ColumnType::kInt64}, {"a", ColumnType::kString}});
  ASSERT_FALSE(dup.ok());
  EXPECT_EQ(dup.error().code, ErrorCode::kMalformedRequest);
}

TEST(TableTest, AppendColumnsAccumulatesAcrossBatches) {
  Table t = SalesSchema();
  ASSERT_FALSE(t.AppendColumns({Strings{"east", "west"}, Ints{1, 2}, Doubles{1.5, 2.5}}));
  ASSERT_FALSE(t.AppendColumns({Strings{"east"}, Ints{3}, Doubles{3.5}}));
  EXPECT_EQ(t.row_count(), 3u);
  auto units = t.FindColumn("units");
  ASSERT_TRUE(units.ok());
  EXPECT_EQ(std::get<Ints>(units.value()->values()), (Ints{1, 2, 3}));
}

TEST(TableTest, AppendEmptyBatchIsNoOp) {
  Table t = SalesSchema();
  ASSERT_FALSE(t.AppendColumns({Strings{}, Ints{}, Doubles{}}));
  EXPECT_EQ(t.row_count(), 0u);
}

TEST(TableTest, AppendIsAtomicOnTypeMismatch) {
  Table t = SalesSchema();
  ASSERT_FALSE(t.AppendColumns({Strings{"a"}, Ints{1}, Doubles{1.0}}));
  // Third vector has the wrong type: nothing from this batch may land.
  auto err = t.AppendColumns({Strings{"b"}, Ints{2}, Ints{2}});
  ASSERT_TRUE(err.has_value());
  EXPECT_EQ(err->code, ErrorCode::kSchemaMismatch);
  EXPECT_EQ(t.row_count(), 1u);
  for (const Column& c : t.columns()) EXPECT_EQ(c.size(), 1u) << c.name();
}

TEST(TableTest, AppendRejectsRaggedAndWrongArity) {
  Table t = SalesSchema();
  auto ragged = t.AppendColumns({Strings{"a", "b"}, Ints{1}, Doubles{1.0, 2.0}});
  ASSERT_TRUE(ragged.has_value());
  EXPECT_EQ(ragged->code, ErrorCode::kSchemaMismatch);
  auto arity = t.AppendColumns({Strings{"a"}, Ints{1}});
  ASSERT_TRUE(arity.has_value());
  EXPECT_EQ(arity->code, ErrorCode::kSchemaMismatch);
  EXPECT_EQ(t.row_count(), 0u);
}

TEST(TableTest, UnknownColumnIsAnErrorValueNotAnException) {
  Table t = SalesSchema();
  auto missing = t.FindColumn("nope");
  ASSERT_FALSE(missing.ok());
  EXPECT_EQ(missing.error().code, ErrorCode::kUnknownColumn);
  EXPECT_NE(missing.error().message.find("nope"), std::string::npos);
  // Column names are case-sensitive.
  EXPECT_FALSE(t.FindColumn("Region").ok());
}

TEST(TableTest, FromColumnsValidatesLengths) {
  auto ok = Table::FromColumns({Column("a", Ints{1, 2}), Column("b", Strings{"x", "y"})});
  ASSERT_TRUE(ok.ok());
  EXPECT_EQ(ok.value().row_count(), 2u);
  auto ragged = Table::FromColumns({Column("a", Ints{1, 2}), Column("b", Strings{"x"})});
  ASSERT_FALSE(ragged.ok());
  EXPECT_EQ(ragged.error().code, ErrorCode::kSchemaMismatch);
}

}  // namespace
}  // namespace analytics::engine
