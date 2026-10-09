// In-process gRPC tests: a real EngineServer on a free port, driven through
// the generated client stubs.
#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "engine/service/engine_service.h"

namespace analytics::engine {
namespace {

namespace pb = ::analytics::v1;
using ::grpc::StatusCode;

class ServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::string error;
    server_ = EngineServer::Start("127.0.0.1", 0, &error);
    ASSERT_NE(server_, nullptr) << error;
    ASSERT_GT(server_->port(), 0);
    channel_ = ::grpc::CreateChannel("127.0.0.1:" + std::to_string(server_->port()),
                                     ::grpc::InsecureChannelCredentials());
    ingest_ = pb::IngestService::NewStub(channel_);
    query_ = pb::QueryService::NewStub(channel_);
  }

  static pb::LoadRequest Header(const std::string& dataset,
                                const std::vector<std::pair<std::string, pb::ColumnType>>& cols) {
    pb::LoadRequest r;
    r.mutable_header()->set_dataset(dataset);
    for (const auto& [name, type] : cols) {
      auto* c = r.mutable_header()->mutable_schema()->add_columns();
      c->set_name(name);
      c->set_type(type);
    }
    return r;
  }

  static pb::LoadRequest SalesBatch(const std::vector<std::string>& region,
                                    const std::vector<int64_t>& units,
                                    const std::vector<double>& price) {
    pb::LoadRequest r;
    auto* b = r.mutable_batch();
    b->set_row_count(static_cast<uint32_t>(region.size()));
    auto* rc = b->add_columns()->mutable_string_values();
    for (const auto& s : region) rc->add_values(s);
    auto* uc = b->add_columns()->mutable_int64_values();
    for (auto u : units) uc->add_values(u);
    auto* pc = b->add_columns()->mutable_double_values();
    for (auto p : price) pc->add_values(p);
    return r;
  }

  static pb::LoadRequest SalesHeader(const std::string& dataset = "sales") {
    return Header(dataset, {{"region", pb::COLUMN_TYPE_STRING},
                            {"units", pb::COLUMN_TYPE_INT64},
                            {"price", pb::COLUMN_TYPE_DOUBLE}});
  }

  // Sends the messages as one Load stream.
  ::grpc::Status Load(const std::vector<pb::LoadRequest>& msgs, pb::LoadResponse* resp) {
    ::grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(10));
    auto writer = ingest_->Load(&ctx, resp);
    for (const auto& m : msgs) {
      if (!writer->Write(m)) break;
    }
    writer->WritesDone();
    return writer->Finish();
  }

  void LoadSales() {
    pb::LoadResponse resp;
    auto s = Load({SalesHeader(),
                   SalesBatch({"west", "east", "west"}, {10, 20, 30}, {1.0, 2.0, 3.0}),
                   SalesBatch({"north", "east"}, {5, 40}, {4.0, 5.0})},
                  &resp);
    ASSERT_TRUE(s.ok()) << s.error_message();
    ASSERT_EQ(resp.rows_loaded(), 5u);
  }

  ::grpc::Status Query(pb::AggregateFunction fn, const std::string& column,
                       std::optional<std::string> group_by, pb::QueryResponse* resp,
                       const std::string& dataset = "sales") {
    pb::QueryRequest req;
    req.set_dataset(dataset);
    req.mutable_aggregation()->set_function(fn);
    req.mutable_aggregation()->set_column(column);
    if (group_by) req.set_group_by(*group_by);
    ::grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(10));
    return query_->Execute(&ctx, req, resp);
  }

  std::unique_ptr<EngineServer> server_;
  std::shared_ptr<::grpc::Channel> channel_;
  std::unique_ptr<pb::IngestService::Stub> ingest_;
  std::unique_ptr<pb::QueryService::Stub> query_;
};

// ---- happy paths ----

TEST_F(ServiceTest, LoadThenGroupedSumIsSortedByKey) {
  LoadSales();
  pb::QueryResponse resp;
  auto s = Query(pb::AGGREGATE_FUNCTION_SUM, "units", "region", &resp);
  ASSERT_TRUE(s.ok()) << s.error_message();
  const pb::Table& t = resp.result();
  ASSERT_EQ(t.schema().columns_size(), 2);
  EXPECT_EQ(t.schema().columns(0).name(), "region");
  EXPECT_EQ(t.schema().columns(0).type(), pb::COLUMN_TYPE_STRING);
  EXPECT_EQ(t.schema().columns(1).name(), "sum(units)");
  EXPECT_EQ(t.schema().columns(1).type(), pb::COLUMN_TYPE_INT64);
  EXPECT_EQ(t.row_count(), 3u);
  ASSERT_EQ(t.columns_size(), 2);
  EXPECT_EQ(t.columns(0).string_values().values(0), "east");
  EXPECT_EQ(t.columns(0).string_values().values(1), "north");
  EXPECT_EQ(t.columns(0).string_values().values(2), "west");
  EXPECT_EQ(t.columns(1).int64_values().values(0), 60);
  EXPECT_EQ(t.columns(1).int64_values().values(1), 5);
  EXPECT_EQ(t.columns(1).int64_values().values(2), 40);
}

TEST_F(ServiceTest, UngroupedAvgIsOneDoubleRow) {
  LoadSales();
  pb::QueryResponse resp;
  ASSERT_TRUE(Query(pb::AGGREGATE_FUNCTION_AVG, "units", std::nullopt, &resp).ok());
  const pb::Table& t = resp.result();
  EXPECT_EQ(t.row_count(), 1u);
  EXPECT_EQ(t.schema().columns(0).name(), "avg(units)");
  EXPECT_EQ(t.schema().columns(0).type(), pb::COLUMN_TYPE_DOUBLE);
  EXPECT_DOUBLE_EQ(t.columns(0).double_values().values(0), 21.0);
}

TEST_F(ServiceTest, CountStarAndMinOfStrings) {
  LoadSales();
  pb::QueryResponse count;
  ASSERT_TRUE(Query(pb::AGGREGATE_FUNCTION_COUNT, "", std::nullopt, &count).ok());
  EXPECT_EQ(count.result().schema().columns(0).name(), "count(*)");
  EXPECT_EQ(count.result().columns(0).int64_values().values(0), 5);
  pb::QueryResponse min;
  ASSERT_TRUE(Query(pb::AGGREGATE_FUNCTION_MIN, "region", std::nullopt, &min).ok());
  EXPECT_EQ(min.result().columns(0).string_values().values(0), "east");
}

TEST_F(ServiceTest, SecondLoadAppendsToExistingDataset) {
  LoadSales();
  pb::LoadResponse resp;
  ASSERT_TRUE(Load({SalesHeader(), SalesBatch({"south"}, {7}, {9.0})}, &resp).ok());
  EXPECT_EQ(resp.rows_loaded(), 1u);
  pb::QueryResponse q;
  ASSERT_TRUE(Query(pb::AGGREGATE_FUNCTION_COUNT, "", std::nullopt, &q).ok());
  EXPECT_EQ(q.result().columns(0).int64_values().values(0), 6);
}

TEST_F(ServiceTest, HeaderOnlyLoadCreatesEmptyDataset) {
  pb::LoadResponse resp;
  ASSERT_TRUE(Load({SalesHeader("empty")}, &resp).ok());
  EXPECT_EQ(resp.rows_loaded(), 0u);
  pb::QueryResponse q;
  ASSERT_TRUE(Query(pb::AGGREGATE_FUNCTION_COUNT, "", std::nullopt, &q, "empty").ok());
  EXPECT_EQ(q.result().columns(0).int64_values().values(0), 0);
  // avg over zero rows has no value (documented in engine/core).
  pb::QueryResponse avg;
  EXPECT_EQ(Query(pb::AGGREGATE_FUNCTION_AVG, "units", std::nullopt, &avg, "empty").error_code(),
            StatusCode::INVALID_ARGUMENT);
}

// ---- Execute errors ----

TEST_F(ServiceTest, UnknownColumnIsInvalidArgument) {
  LoadSales();
  pb::QueryResponse resp;
  auto agg = Query(pb::AGGREGATE_FUNCTION_SUM, "nope", std::nullopt, &resp);
  EXPECT_EQ(agg.error_code(), StatusCode::INVALID_ARGUMENT);
  EXPECT_NE(agg.error_message().find("nope"), std::string::npos);
  auto grp = Query(pb::AGGREGATE_FUNCTION_SUM, "units", "nope", &resp);
  EXPECT_EQ(grp.error_code(), StatusCode::INVALID_ARGUMENT);
}

TEST_F(ServiceTest, SumOverStringIsInvalidArgument) {
  LoadSales();
  pb::QueryResponse resp;
  auto s = Query(pb::AGGREGATE_FUNCTION_SUM, "region", std::nullopt, &resp);
  EXPECT_EQ(s.error_code(), StatusCode::INVALID_ARGUMENT);
  EXPECT_NE(s.error_message().find("region"), std::string::npos);
}

TEST_F(ServiceTest, UnspecifiedOrUnknownFunctionAndMissingColumnAreInvalidArgument) {
  LoadSales();
  pb::QueryResponse resp;
  EXPECT_EQ(Query(pb::AGGREGATE_FUNCTION_UNSPECIFIED, "units", std::nullopt, &resp).error_code(),
            StatusCode::INVALID_ARGUMENT);
  EXPECT_EQ(Query(static_cast<pb::AggregateFunction>(99), "units", std::nullopt, &resp)
                .error_code(),
            StatusCode::INVALID_ARGUMENT);
  EXPECT_EQ(Query(pb::AGGREGATE_FUNCTION_SUM, "", std::nullopt, &resp).error_code(),
            StatusCode::INVALID_ARGUMENT);
}

TEST_F(ServiceTest, UnknownDatasetIsNotFoundAndEmptyNameIsInvalid) {
  pb::QueryResponse resp;
  EXPECT_EQ(Query(pb::AGGREGATE_FUNCTION_COUNT, "", std::nullopt, &resp, "ghost").error_code(),
            StatusCode::NOT_FOUND);
  EXPECT_EQ(Query(pb::AGGREGATE_FUNCTION_COUNT, "", std::nullopt, &resp, "").error_code(),
            StatusCode::INVALID_ARGUMENT);
}

// ---- Load errors ----

TEST_F(ServiceTest, MalformedStreamsAreInvalidArgument) {
  pb::LoadResponse resp;
  // Empty stream.
  EXPECT_EQ(Load({}, &resp).error_code(), StatusCode::INVALID_ARGUMENT);
  // Batch before header.
  EXPECT_EQ(Load({SalesBatch({"a"}, {1}, {1.0})}, &resp).error_code(),
            StatusCode::INVALID_ARGUMENT);
  // Empty message.
  EXPECT_EQ(Load({pb::LoadRequest()}, &resp).error_code(), StatusCode::INVALID_ARGUMENT);
  // Second header.
  EXPECT_EQ(Load({SalesHeader(), SalesHeader()}, &resp).error_code(),
            StatusCode::INVALID_ARGUMENT);
  // Empty dataset name.
  EXPECT_EQ(Load({SalesHeader("")}, &resp).error_code(), StatusCode::INVALID_ARGUMENT);
  // UNSPECIFIED column type.
  EXPECT_EQ(Load({Header("d", {{"a", pb::COLUMN_TYPE_UNSPECIFIED}})}, &resp).error_code(),
            StatusCode::INVALID_ARGUMENT);
  // Duplicate column names.
  EXPECT_EQ(Load({Header("d", {{"a", pb::COLUMN_TYPE_INT64}, {"a", pb::COLUMN_TYPE_INT64}})},
                 &resp)
                .error_code(),
            StatusCode::INVALID_ARGUMENT);
}

TEST_F(ServiceTest, BatchNotMatchingSchemaIsInvalidArgument) {
  pb::LoadResponse resp;
  // Wrong vector count.
  pb::LoadRequest few = SalesBatch({"a"}, {1}, {1.0});
  few.mutable_batch()->mutable_columns()->RemoveLast();
  EXPECT_EQ(Load({SalesHeader(), few}, &resp).error_code(), StatusCode::INVALID_ARGUMENT);
  // Wrong vector type (units sent as doubles).
  pb::LoadRequest wrong = SalesBatch({"a"}, {1}, {1.0});
  wrong.mutable_batch()->mutable_columns(1)->mutable_double_values()->add_values(1.0);
  EXPECT_EQ(Load({SalesHeader(), wrong}, &resp).error_code(), StatusCode::INVALID_ARGUMENT);
  // Ragged: row_count disagrees with the vectors.
  pb::LoadRequest ragged = SalesBatch({"a", "b"}, {1, 2}, {1.0, 2.0});
  ragged.mutable_batch()->set_row_count(3);
  EXPECT_EQ(Load({SalesHeader(), ragged}, &resp).error_code(), StatusCode::INVALID_ARGUMENT);
}

TEST_F(ServiceTest, FailedLoadLeavesNoDataset) {
  pb::LoadResponse resp;
  pb::LoadRequest bad = SalesBatch({"b"}, {2}, {2.0});
  bad.mutable_batch()->set_row_count(9);
  // A good batch first, then a bad one: all or nothing.
  EXPECT_EQ(Load({SalesHeader("atomic"), SalesBatch({"a"}, {1}, {1.0}), bad}, &resp).error_code(),
            StatusCode::INVALID_ARGUMENT);
  pb::QueryResponse q;
  EXPECT_EQ(Query(pb::AGGREGATE_FUNCTION_COUNT, "", std::nullopt, &q, "atomic").error_code(),
            StatusCode::NOT_FOUND);
}

TEST_F(ServiceTest, FailedLoadLeavesExistingDatasetUnchanged) {
  LoadSales();
  pb::LoadResponse resp;
  pb::LoadRequest bad = SalesBatch({"b"}, {2}, {2.0});
  bad.mutable_batch()->set_row_count(9);
  EXPECT_EQ(Load({SalesHeader(), SalesBatch({"z"}, {1}, {1.0}), bad}, &resp).error_code(),
            StatusCode::INVALID_ARGUMENT);
  pb::QueryResponse q;
  ASSERT_TRUE(Query(pb::AGGREGATE_FUNCTION_COUNT, "", std::nullopt, &q).ok());
  EXPECT_EQ(q.result().columns(0).int64_values().values(0), 5);
}

TEST_F(ServiceTest, SchemaMismatchWithStoredDatasetIsInvalidArgument) {
  LoadSales();
  pb::LoadResponse resp;
  // Different type.
  auto s = Load({Header("sales", {{"region", pb::COLUMN_TYPE_STRING},
                                  {"units", pb::COLUMN_TYPE_DOUBLE},
                                  {"price", pb::COLUMN_TYPE_DOUBLE}})},
                &resp);
  EXPECT_EQ(s.error_code(), StatusCode::INVALID_ARGUMENT);
  // Different order.
  EXPECT_EQ(Load({Header("sales", {{"units", pb::COLUMN_TYPE_INT64},
                                   {"region", pb::COLUMN_TYPE_STRING},
                                   {"price", pb::COLUMN_TYPE_DOUBLE}})},
                 &resp)
                .error_code(),
            StatusCode::INVALID_ARGUMENT);
}

// ---- concurrency (also the TSAN target in Phase 9) ----

TEST_F(ServiceTest, ConcurrentLoadsAndQueriesAreConsistent) {
  constexpr int kWriters = 4, kLoadsEach = 10, kReaders = 4;
  pb::LoadResponse init;
  ASSERT_TRUE(Load({SalesHeader()}, &init).ok());
  std::atomic<bool> stop{false};
  std::atomic<int> reader_failures{0};

  std::vector<std::thread> readers;
  for (int r = 0; r < kReaders; ++r) {
    readers.emplace_back([&] {
      while (!stop) {
        pb::QueryResponse resp;
        auto s = Query(pb::AGGREGATE_FUNCTION_COUNT, "", std::nullopt, &resp);
        // Each load adds 2 rows atomically: the count is always even.
        if (!s.ok() || resp.result().columns(0).int64_values().values(0) % 2 != 0) {
          ++reader_failures;
        }
      }
    });
  }
  std::vector<std::thread> writers;
  for (int w = 0; w < kWriters; ++w) {
    writers.emplace_back([&] {
      for (int i = 0; i < kLoadsEach; ++i) {
        pb::LoadResponse resp;
        auto s = Load({SalesHeader(), SalesBatch({"a"}, {1}, {1.0}),
                       SalesBatch({"b"}, {2}, {2.0})},
                      &resp);
        EXPECT_TRUE(s.ok()) << s.error_message();
      }
    });
  }
  for (auto& t : writers) t.join();
  stop = true;
  for (auto& t : readers) t.join();
  EXPECT_EQ(reader_failures.load(), 0);

  pb::QueryResponse resp;
  ASSERT_TRUE(Query(pb::AGGREGATE_FUNCTION_COUNT, "", std::nullopt, &resp).ok());
  EXPECT_EQ(resp.result().columns(0).int64_values().values(0), kWriters * kLoadsEach * 2);
}

TEST(EngineServerTest, PortZeroPicksDistinctFreePorts) {
  std::string error;
  auto a = EngineServer::Start("127.0.0.1", 0, &error);
  auto b = EngineServer::Start("127.0.0.1", 0, &error);
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  EXPECT_NE(a->port(), b->port());
}

}  // namespace
}  // namespace analytics::engine
