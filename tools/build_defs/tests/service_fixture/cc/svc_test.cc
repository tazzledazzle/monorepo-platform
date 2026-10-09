#include <gtest/gtest.h>

#include "tools/build_defs/tests/service_fixture/cc/svc.h"

TEST(Svc, EchoesPoint) {
  fixture::Svc svc;
  ::grpc::ServerContext ctx;
  fixture::types::v1::Point p;
  p.set_x(3);
  fixture::service::v1::Reply r;
  EXPECT_TRUE(svc.Get(&ctx, &p, &r).ok());
  EXPECT_EQ(r.p().x(), 3);
}
