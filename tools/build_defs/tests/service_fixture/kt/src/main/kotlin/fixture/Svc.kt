package fixture

import fixture.service.v1.FixtureGrpc
import fixture.service.v1.Reply
import fixture.types.v1.Point
import io.grpc.stub.StreamObserver

class Svc : FixtureGrpc.FixtureImplBase() {
    override fun get(request: Point, responseObserver: StreamObserver<Reply>) {
        responseObserver.onNext(Reply.newBuilder().setP(request).build())
        responseObserver.onCompleted()
    }
}
