#include "engine/core/aggregate.h"

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

namespace analytics::engine {
namespace {

using Ints = std::vector<int64_t>;
using Doubles = std::vector<double>;
using Strings = std::vector<std::string>;

// region, units, price, with deliberately unsorted regions.
Table Sales() {
  auto t = Table::FromColumns({
      Column("region", Strings{"west", "east", "west", "north", "east", "west"}),
      Column("units", Ints{10, 20, 30, 5, 40, 1}),
      Column("price", Doubles{1.0, 2.0, 3.0, 4.0, 5.0, 6.0}),
  });
  EXPECT_TRUE(t.ok());
  return std::move(t).value();
}

Table Empty() {
  auto t = Table::Create({{"g", ColumnType::kString}, {"v", ColumnType::kInt64}});
  EXPECT_TRUE(t.ok());
  return std::move(t).value();
}

template <typename V>
V Col(const Table& t, size_t i) {
  return std::get<V>(t.columns().at(i).values());
}

Table Agg(const Table& t, AggregateFunction f, const std::string& col,
          std::optional<std::string> group = std::nullopt) {
  auto r = Aggregate(t, {f, col}, group);
  EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
  return std::move(r).value();
}

TEST(AggregateTest, SumOfIntsKeepsInt64) {
  Table r = Agg(Sales(), AggregateFunction::kSum, "units");
  ASSERT_EQ(r.row_count(), 1u);
  ASSERT_EQ(r.column_count(), 1u);
  EXPECT_EQ(r.columns()[0].name(), "sum(units)");
  EXPECT_EQ(Col<Ints>(r, 0), Ints{106});
}

TEST(AggregateTest, SumOfDoubles) {
  Table r = Agg(Sales(), AggregateFunction::kSum, "price");
  EXPECT_DOUBLE_EQ(Col<Doubles>(r, 0)[0], 21.0);
}

TEST(AggregateTest, AvgOfIntsReturnsDouble) {
  Table r = Agg(Sales(), AggregateFunction::kAvg, "units");
  EXPECT_EQ(r.columns()[0].name(), "avg(units)");
  EXPECT_EQ(r.columns()[0].type(), ColumnType::kDouble);
  EXPECT_DOUBLE_EQ(Col<Doubles>(r, 0)[0], 106.0 / 6.0);
}

TEST(AggregateTest, CountStarAndCountOfColumnIncludingStrings) {
  Table star = Agg(Sales(), AggregateFunction::kCount, "");
  EXPECT_EQ(star.columns()[0].name(), "count(*)");
  EXPECT_EQ(Col<Ints>(star, 0), Ints{6});
  Table str = Agg(Sales(), AggregateFunction::kCount, "region");
  EXPECT_EQ(str.columns()[0].name(), "count(region)");
  EXPECT_EQ(str.columns()[0].type(), ColumnType::kInt64);
  EXPECT_EQ(Col<Ints>(str, 0), Ints{6});
}

TEST(AggregateTest, MinMaxOnNumbers) {
  EXPECT_EQ(Col<Ints>(Agg(Sales(), AggregateFunction::kMin, "units"), 0), Ints{1});
  EXPECT_EQ(Col<Ints>(Agg(Sales(), AggregateFunction::kMax, "units"), 0), Ints{40});
  EXPECT_DOUBLE_EQ(Col<Doubles>(Agg(Sales(), AggregateFunction::kMax, "price"), 0)[0], 6.0);
}

TEST(AggregateTest, MinMaxOnStringsUseByteOrder) {
  Table r = Agg(Sales(), AggregateFunction::kMin, "region");
  EXPECT_EQ(r.columns()[0].type(), ColumnType::kString);
  EXPECT_EQ(Col<Strings>(r, 0), Strings{"east"});
  EXPECT_EQ(Col<Strings>(Agg(Sales(), AggregateFunction::kMax, "region"), 0), Strings{"west"});
  // Byte order, not locale: uppercase sorts before lowercase.
  auto t = Table::FromColumns({Column("s", Strings{"b", "B", "a"})});
  EXPECT_EQ(Col<Strings>(Agg(t.value(), AggregateFunction::kMin, "s"), 0), Strings{"B"});
}

TEST(AggregateTest, SumAndAvgOfStringsAreInvalid) {
  for (auto f : {AggregateFunction::kSum, AggregateFunction::kAvg}) {
    auto r = Aggregate(Sales(), {f, "region"});
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::kInvalidAggregation);
    EXPECT_NE(r.error().message.find("region"), std::string::npos);
  }
}

TEST(AggregateTest, MissingColumnForNonCountIsInvalid) {
  for (auto f : {AggregateFunction::kSum, AggregateFunction::kAvg, AggregateFunction::kMin,
                 AggregateFunction::kMax}) {
    auto r = Aggregate(Sales(), {f, ""});
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::kInvalidAggregation);
  }
}

TEST(AggregateTest, UnknownColumnErrors) {
  auto agg = Aggregate(Sales(), {AggregateFunction::kSum, "nope"});
  ASSERT_FALSE(agg.ok());
  EXPECT_EQ(agg.error().code, ErrorCode::kUnknownColumn);
  auto grp = Aggregate(Sales(), {AggregateFunction::kSum, "units"}, "nope");
  ASSERT_FALSE(grp.ok());
  EXPECT_EQ(grp.error().code, ErrorCode::kUnknownColumn);
  auto cnt = Aggregate(Sales(), {AggregateFunction::kCount, "nope"});
  ASSERT_FALSE(cnt.ok());
  EXPECT_EQ(cnt.error().code, ErrorCode::kUnknownColumn);
}

TEST(AggregateTest, IntSumWrapsInsteadOfUndefinedBehavior) {
  auto t = Table::FromColumns(
      {Column("v", Ints{std::numeric_limits<int64_t>::max(), 1})});
  Table r = Agg(t.value(), AggregateFunction::kSum, "v");
  EXPECT_EQ(Col<Ints>(r, 0)[0], std::numeric_limits<int64_t>::min());
}

// ---- empty input ----

TEST(AggregateTest, EmptyTableUngrouped) {
  EXPECT_EQ(Col<Ints>(Agg(Empty(), AggregateFunction::kCount, ""), 0), Ints{0});
  EXPECT_EQ(Col<Ints>(Agg(Empty(), AggregateFunction::kSum, "v"), 0), Ints{0});
  for (auto f : {AggregateFunction::kAvg, AggregateFunction::kMin, AggregateFunction::kMax}) {
    auto r = Aggregate(Empty(), {f, "v"});
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::kInvalidAggregation);
  }
}

TEST(AggregateTest, EmptyTableGroupedHasZeroRows) {
  Table r = Agg(Empty(), AggregateFunction::kAvg, "v", "g");
  EXPECT_EQ(r.row_count(), 0u);
  EXPECT_EQ(r.column_count(), 2u);
}

TEST(AggregateTest, EmptyTableStillValidatesColumns) {
  EXPECT_EQ(Aggregate(Empty(), {AggregateFunction::kSum, "nope"}).error().code,
            ErrorCode::kUnknownColumn);
}

// ---- group_by ----

TEST(GroupByTest, ResultsSortedByKeyWithKeyColumnFirst) {
  Table r = Agg(Sales(), AggregateFunction::kSum, "units", "region");
  ASSERT_EQ(r.column_count(), 2u);
  EXPECT_EQ(r.columns()[0].name(), "region");
  EXPECT_EQ(r.columns()[1].name(), "sum(units)");
  EXPECT_EQ(Col<Strings>(r, 0), (Strings{"east", "north", "west"}));
  EXPECT_EQ(Col<Ints>(r, 1), (Ints{60, 5, 41}));
}

TEST(GroupByTest, EveryFunctionGrouped) {
  Table avg = Agg(Sales(), AggregateFunction::kAvg, "units", "region");
  EXPECT_EQ(Col<Doubles>(avg, 1), (Doubles{30.0, 5.0, 41.0 / 3.0}));
  Table cnt = Agg(Sales(), AggregateFunction::kCount, "", "region");
  EXPECT_EQ(cnt.columns()[1].name(), "count(*)");
  EXPECT_EQ(Col<Ints>(cnt, 1), (Ints{2, 1, 3}));
  Table mn = Agg(Sales(), AggregateFunction::kMin, "price", "region");
  EXPECT_EQ(Col<Doubles>(mn, 1), (Doubles{2.0, 4.0, 1.0}));
  Table mx = Agg(Sales(), AggregateFunction::kMax, "units", "region");
  EXPECT_EQ(Col<Ints>(mx, 1), (Ints{40, 5, 30}));
}

TEST(GroupByTest, IntKeysSortNumericallyNotLexically) {
  auto t = Table::FromColumns({Column("k", Ints{10, 2, -5, 2}), Column("v", Ints{1, 1, 1, 1})});
  Table r = Agg(t.value(), AggregateFunction::kCount, "", "k");
  EXPECT_EQ(Col<Ints>(r, 0), (Ints{-5, 2, 10}));
  EXPECT_EQ(Col<Ints>(r, 1), (Ints{1, 2, 1}));
}

TEST(GroupByTest, DoubleKeysSortWithNaNLast) {
  const double nan = std::nan("");
  auto t = Table::FromColumns({Column("k", Doubles{2.5, nan, -1.0, nan}), Column("v", Ints{1, 2, 3, 4})});
  Table r = Agg(t.value(), AggregateFunction::kSum, "v", "k");
  const auto& keys = Col<Doubles>(r, 0);
  ASSERT_EQ(keys.size(), 3u);
  EXPECT_DOUBLE_EQ(keys[0], -1.0);
  EXPECT_DOUBLE_EQ(keys[1], 2.5);
  EXPECT_TRUE(std::isnan(keys[2]));
  EXPECT_EQ(Col<Ints>(r, 1), (Ints{3, 1, 6}));
}

TEST(GroupByTest, GroupingByTheAggregatedColumn) {
  Table r = Agg(Sales(), AggregateFunction::kMax, "region", "region");
  EXPECT_EQ(Col<Strings>(r, 0), Col<Strings>(r, 1));
}

TEST(GroupByTest, NoGroupByIsSingleRow) {
  EXPECT_EQ(Agg(Sales(), AggregateFunction::kCount, "").row_count(), 1u);
}

TEST(AggregateTest, DoubleMinMaxIgnoreNaNUnlessAllNaN) {
  const double nan = std::nan("");
  auto t = Table::FromColumns({Column("v", Doubles{nan, 3.0, 1.0})});
  EXPECT_DOUBLE_EQ(Col<Doubles>(Agg(t.value(), AggregateFunction::kMin, "v"), 0)[0], 1.0);
  EXPECT_DOUBLE_EQ(Col<Doubles>(Agg(t.value(), AggregateFunction::kMax, "v"), 0)[0], 3.0);
  auto all = Table::FromColumns({Column("v", Doubles{nan, nan})});
  EXPECT_TRUE(std::isnan(Col<Doubles>(Agg(all.value(), AggregateFunction::kMax, "v"), 0)[0]));
}

}  // namespace
}  // namespace analytics::engine
