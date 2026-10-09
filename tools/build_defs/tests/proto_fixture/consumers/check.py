from tools.build_defs.tests.proto_fixture.service import service_pb2, service_pb2_grpc
from tools.build_defs.tests.proto_fixture.types import types_pb2

r = service_pb2.Reply(p=types_pb2.Point(x=7))
assert r.p.x == 7
print("py ok", service_pb2_grpc.FixtureStub.__name__)
