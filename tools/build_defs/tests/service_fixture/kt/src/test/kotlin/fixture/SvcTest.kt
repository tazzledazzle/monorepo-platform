package fixture

import fixture.service.v1.FixtureGrpc
import fixture.types.v1.Point
import io.grpc.inprocess.InProcessChannelBuilder
import io.grpc.inprocess.InProcessServerBuilder
import org.junit.Assert.assertEquals
import org.junit.Test

class SvcTest {
    @Test
    fun echoesPointOverInProcessChannel() {
        val server = InProcessServerBuilder.forName("svc-test").directExecutor().addService(Svc()).build().start()
        val channel = InProcessChannelBuilder.forName("svc-test").directExecutor().build()
        try {
            val reply = FixtureGrpc.newBlockingStub(channel).get(Point.newBuilder().setX(3).build())
            assertEquals(3, reply.p.x)
        } finally {
            channel.shutdownNow()
            server.shutdownNow()
        }
    }
}
