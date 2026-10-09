#ifndef ENGINE_SERVICE_ENGINE_SERVICE_H_
#define ENGINE_SERVICE_ENGINE_SERVICE_H_

#include <grpcpp/grpcpp.h>

#include <memory>
#include <string>

#include "analytics/v1/ingest.grpc.pb.h"
#include "analytics/v1/query.grpc.pb.h"
#include "engine/service/dataset_store.h"

namespace analytics::engine {

// IngestService.Load and QueryService.Execute over a shared DatasetStore.
// Status mapping (docs/proto-compatibility.md): every engine error is
// INVALID_ARGUMENT, except an unknown dataset on Execute, which is NOT_FOUND.
class EngineService final : public ::analytics::v1::IngestService::Service,
                            public ::analytics::v1::QueryService::Service {
 public:
  explicit EngineService(DatasetStore* store) : store_(store) {}

  ::grpc::Status Load(::grpc::ServerContext* context,
                      ::grpc::ServerReader<::analytics::v1::LoadRequest>* reader,
                      ::analytics::v1::LoadResponse* response) override;

  ::grpc::Status Execute(::grpc::ServerContext* context,
                         const ::analytics::v1::QueryRequest* request,
                         ::analytics::v1::QueryResponse* response) override;

 private:
  DatasetStore* store_;
};

// A running gRPC server hosting EngineService. Used by main() and by tests.
class EngineServer {
 public:
  // Binds `host:port` (port 0 picks a free one). Returns null and fills
  // `error` if the port cannot be bound.
  static std::unique_ptr<EngineServer> Start(const std::string& host, int port,
                                             std::string* error);
  ~EngineServer();

  int port() const { return port_; }
  void Shutdown();

 private:
  EngineServer() = default;
  DatasetStore store_;
  std::unique_ptr<EngineService> service_;
  std::unique_ptr<::grpc::Server> server_;
  int port_ = 0;
};

}  // namespace analytics::engine

#endif  // ENGINE_SERVICE_ENGINE_SERVICE_H_
