#pragma once
#include "tools/build_defs/tests/proto_fixture/service/service.grpc.pb.h"

namespace fixture {
class Svc final : public ::fixture::service::v1::Fixture::Service {
 public:
  ::grpc::Status Get(::grpc::ServerContext*, const ::fixture::types::v1::Point* p,
                     ::fixture::service::v1::Reply* r) override;
};
}  // namespace fixture
