#include "tools/build_defs/tests/service_fixture/cc/svc.h"

namespace fixture {
::grpc::Status Svc::Get(::grpc::ServerContext*, const ::fixture::types::v1::Point* p,
                        ::fixture::service::v1::Reply* r) {
  *r->mutable_p() = *p;
  return ::grpc::Status::OK;
}
}  // namespace fixture
