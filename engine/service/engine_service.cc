#include "engine/service/engine_service.h"

#include <chrono>
#include <optional>
#include <utility>
#include <vector>

#include "engine/core/aggregate.h"

namespace analytics::engine {
namespace {

namespace pb = ::analytics::v1;
using ::grpc::Status;
using ::grpc::StatusCode;

Status Invalid(const std::string& message) {
  return Status(StatusCode::INVALID_ARGUMENT, message);
}

Status ToStatus(const Error& e) { return Invalid(e.message); }

std::optional<ColumnType> FromProto(pb::ColumnType t) {
  switch (t) {
    case pb::COLUMN_TYPE_INT64: return ColumnType::kInt64;
    case pb::COLUMN_TYPE_DOUBLE: return ColumnType::kDouble;
    case pb::COLUMN_TYPE_STRING: return ColumnType::kString;
    default: return std::nullopt;  // UNSPECIFIED or a value from a newer client
  }
}

pb::ColumnType ToProto(ColumnType t) {
  switch (t) {
    case ColumnType::kInt64: return pb::COLUMN_TYPE_INT64;
    case ColumnType::kDouble: return pb::COLUMN_TYPE_DOUBLE;
    case ColumnType::kString: return pb::COLUMN_TYPE_STRING;
  }
  return pb::COLUMN_TYPE_UNSPECIFIED;
}

// Checks the wire vector against the expected type and row count, then copies it.
Result<ColumnValues> FromProto(const pb::ColumnData& data, const ColumnSpec& spec,
                               uint32_t row_count) {
  auto bad = [&](const char* what) {
    return Error{ErrorCode::kSchemaMismatch,
                 "column '" + spec.name + "': " + what};
  };
  switch (spec.type) {
    case ColumnType::kInt64:
      if (data.values_case() != pb::ColumnData::kInt64Values) return bad("expected an int64 vector");
      if (data.int64_values().values_size() != static_cast<int>(row_count)) return bad("length differs from row_count");
      return ColumnValues(std::vector<int64_t>(data.int64_values().values().begin(),
                                               data.int64_values().values().end()));
    case ColumnType::kDouble:
      if (data.values_case() != pb::ColumnData::kDoubleValues) return bad("expected a double vector");
      if (data.double_values().values_size() != static_cast<int>(row_count)) return bad("length differs from row_count");
      return ColumnValues(std::vector<double>(data.double_values().values().begin(),
                                              data.double_values().values().end()));
    case ColumnType::kString:
      if (data.values_case() != pb::ColumnData::kStringValues) return bad("expected a string vector");
      if (data.string_values().values_size() != static_cast<int>(row_count)) return bad("length differs from row_count");
      return ColumnValues(std::vector<std::string>(data.string_values().values().begin(),
                                                   data.string_values().values().end()));
  }
  return bad("unsupported type");
}

void ToProto(const Column& column, pb::ColumnData* out) {
  std::visit(
      [&](const auto& v) {
        using T = typename std::decay_t<decltype(v)>::value_type;
        if constexpr (std::is_same_v<T, int64_t>) {
          out->mutable_int64_values()->mutable_values()->Assign(v.begin(), v.end());
        } else if constexpr (std::is_same_v<T, double>) {
          out->mutable_double_values()->mutable_values()->Assign(v.begin(), v.end());
        } else {
          out->mutable_string_values()->mutable_values()->Assign(v.begin(), v.end());
        }
      },
      column.values());
}

}  // namespace

Status EngineService::Load(::grpc::ServerContext*, ::grpc::ServerReader<pb::LoadRequest>* reader,
                           pb::LoadResponse* response) {
  pb::LoadRequest msg;
  if (!reader->Read(&msg)) return Invalid("stream must start with a header");
  if (msg.payload_case() != pb::LoadRequest::kHeader) {
    return Invalid("first message must be a header");
  }
  // Copy: `msg` is overwritten by every later Read.
  const pb::LoadHeader header = msg.header();
  if (header.dataset().empty()) return Invalid("dataset name must not be empty");

  std::vector<ColumnSpec> schema;
  for (const pb::Column& c : header.schema().columns()) {
    auto type = FromProto(c.type());
    if (!type) return Invalid("column '" + c.name() + "' has an unspecified or unknown type");
    schema.push_back({c.name(), *type});
  }
  auto staged = Table::Create(schema);
  if (!staged.ok()) return ToStatus(staged.error());
  Table table = std::move(staged).value();

  // Fail before reading the batches when the schema cannot match.
  if (auto existing = store_->SchemaOf(header.dataset()); existing && *existing != schema) {
    return Invalid("schema of dataset '" + header.dataset() + "' differs from the stored schema");
  }

  while (reader->Read(&msg)) {
    if (msg.payload_case() == pb::LoadRequest::kHeader) return Invalid("duplicate header");
    if (msg.payload_case() != pb::LoadRequest::kBatch) return Invalid("empty message in stream");
    const pb::RowBatch& batch = msg.batch();
    if (static_cast<size_t>(batch.columns_size()) != schema.size()) {
      return Invalid("batch has " + std::to_string(batch.columns_size()) +
                     " columns, schema has " + std::to_string(schema.size()));
    }
    std::vector<ColumnValues> values;
    values.reserve(schema.size());
    for (size_t i = 0; i < schema.size(); ++i) {
      auto v = FromProto(batch.columns(static_cast<int>(i)), schema[i], batch.row_count());
      if (!v.ok()) return ToStatus(v.error());
      values.push_back(std::move(v).value());
    }
    if (auto err = table.AppendColumns(std::move(values))) return ToStatus(*err);
    // A zero-column schema carries rows only as a count.
  }

  const size_t loaded = table.row_count();
  auto total = store_->Append(header.dataset(), table);
  if (!total.ok()) return ToStatus(total.error());
  response->set_rows_loaded(loaded);
  return Status::OK;
}

Status EngineService::Execute(::grpc::ServerContext*, const pb::QueryRequest* request,
                              pb::QueryResponse* response) {
  if (request->dataset().empty()) return Invalid("dataset name must not be empty");
  Aggregation agg;
  switch (request->aggregation().function()) {
    case pb::AGGREGATE_FUNCTION_SUM: agg.function = AggregateFunction::kSum; break;
    case pb::AGGREGATE_FUNCTION_AVG: agg.function = AggregateFunction::kAvg; break;
    case pb::AGGREGATE_FUNCTION_COUNT: agg.function = AggregateFunction::kCount; break;
    case pb::AGGREGATE_FUNCTION_MIN: agg.function = AggregateFunction::kMin; break;
    case pb::AGGREGATE_FUNCTION_MAX: agg.function = AggregateFunction::kMax; break;
    default: return Invalid("aggregation function is unspecified or unknown");
  }
  agg.column = request->aggregation().column();

  DatasetStore::Snapshot table = store_->Get(request->dataset());
  if (!table) {
    return Status(StatusCode::NOT_FOUND, "unknown dataset '" + request->dataset() + "'");
  }
  std::optional<std::string> group_by;
  if (request->has_group_by()) group_by = request->group_by();

  auto result = Aggregate(*table, agg, group_by);
  if (!result.ok()) return ToStatus(result.error());

  const Table& out = result.value();
  pb::Table* wire = response->mutable_result();
  for (const Column& c : out.columns()) {
    pb::Column* col = wire->mutable_schema()->add_columns();
    col->set_name(c.name());
    col->set_type(ToProto(c.type()));
    ToProto(c, wire->add_columns());
  }
  wire->set_row_count(static_cast<uint32_t>(out.row_count()));
  return Status::OK;
}

std::unique_ptr<EngineServer> EngineServer::Start(const std::string& host, int port,
                                                  std::string* error) {
  std::unique_ptr<EngineServer> s(new EngineServer());
  s->service_ = std::make_unique<EngineService>(&s->store_);
  ::grpc::ServerBuilder builder;
  int bound = 0;
  builder.AddListeningPort(host + ":" + std::to_string(port),
                           ::grpc::InsecureServerCredentials(), &bound);
  builder.RegisterService(static_cast<pb::IngestService::Service*>(s->service_.get()));
  builder.RegisterService(static_cast<pb::QueryService::Service*>(s->service_.get()));
  s->server_ = builder.BuildAndStart();
  if (!s->server_ || bound == 0) {
    if (error) *error = "failed to listen on " + host + ":" + std::to_string(port);
    return nullptr;
  }
  s->port_ = bound;
  return s;
}

void EngineServer::Shutdown() {
  if (server_) {
    server_->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds(2));
    server_->Wait();
  }
}

EngineServer::~EngineServer() { Shutdown(); }

}  // namespace analytics::engine
