package analytics.gateway

import com.example.analytics.v1.AggregateFunction
import com.example.analytics.v1.Aggregation
import com.example.analytics.v1.QueryRequest
import com.example.analytics.v1.QueryResponse
import com.example.analytics.v1.QueryServiceGrpc
import com.example.analytics.v1.Table
import io.grpc.ManagedChannel
import io.grpc.Server
import io.grpc.Status
import io.grpc.StatusRuntimeException
import io.grpc.netty.NettyChannelBuilder
import io.grpc.netty.NettyServerBuilder
import io.grpc.stub.StreamObserver
import java.net.ServerSocket
import java.util.concurrent.TimeUnit
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

class GatewayTest {
  private val cleanup = mutableListOf<() -> Unit>()

  @After
  fun tearDown() {
    cleanup.reversed().forEach { it() }
  }

  /** Fake engine: `handler` decides the answer. Returns its port. */
  private fun fakeEngine(handler: (QueryRequest, StreamObserver<QueryResponse>) -> Unit): Int {
    val server: Server =
        NettyServerBuilder.forPort(0)
            .addService(
                object : QueryServiceGrpc.QueryServiceImplBase() {
                  override fun execute(r: QueryRequest, o: StreamObserver<QueryResponse>) = handler(r, o)
                })
            .build()
            .start()
    cleanup.add { server.shutdownNow() }
    return server.port
  }

  private fun client(engineTarget: String, deadlineMillis: Long = 30_000): QueryServiceGrpc.QueryServiceBlockingStub {
    val gateway = GatewayServer(0, engineTarget, engineDeadlineMillis = deadlineMillis)
    val port = gateway.start()
    cleanup.add { gateway.stop() }
    val channel: ManagedChannel = NettyChannelBuilder.forAddress("localhost", port).usePlaintext().build()
    cleanup.add { channel.shutdownNow().awaitTermination(2, TimeUnit.SECONDS) }
    return QueryServiceGrpc.newBlockingStub(channel).withDeadlineAfter(10, TimeUnit.SECONDS)
  }

  private fun sumRevenue(dataset: String) =
      QueryRequest.newBuilder()
          .setDataset(dataset)
          .setAggregation(
              Aggregation.newBuilder()
                  .setFunction(AggregateFunction.AGGREGATE_FUNCTION_SUM)
                  .setColumn("revenue"))
          .build()

  private fun statusOf(block: () -> Unit): Status {
    try {
      block()
    } catch (e: StatusRuntimeException) {
      return e.status
    }
    fail("expected a gRPC error")
    throw AssertionError()
  }

  @Test
  fun forwardsValidQueryAndReturnsEngineResult() {
    var seen: QueryRequest? = null
    val port = fakeEngine { r, o ->
      seen = r
      o.onNext(QueryResponse.newBuilder().setResult(Table.newBuilder().setRowCount(7)).build())
      o.onCompleted()
    }
    val response = client("localhost:$port").execute(sumRevenue("sales"))
    assertEquals(7, response.result.rowCount)
    assertEquals("sales", seen?.dataset)
  }

  @Test
  fun unknownDatasetIsNotFoundAndEngineIsNotCalled() {
    var called = false
    val port = fakeEngine { _, o ->
      called = true
      o.onCompleted()
    }
    val status = statusOf { client("localhost:$port").execute(sumRevenue("nope")) }
    assertEquals(Status.Code.NOT_FOUND, status.code)
    assertTrue(status.description!!.contains("nope"))
    assertEquals(false, called)
  }

  @Test
  fun unreachableEngineIsUnavailableQuickly() {
    val closedPort = ServerSocket(0).use { it.localPort }
    val start = System.nanoTime()
    val status = statusOf { client("localhost:$closedPort").execute(sumRevenue("sales")) }
    assertEquals(Status.Code.UNAVAILABLE, status.code)
    assertTrue(TimeUnit.NANOSECONDS.toSeconds(System.nanoTime() - start) < 10)
  }

  @Test
  fun slowEngineBeyondGatewayDeadlineIsUnavailable() {
    val port = fakeEngine { _, _ -> /* never answers */ }
    val status = statusOf { client("localhost:$port", deadlineMillis = 200).execute(sumRevenue("sales")) }
    assertEquals(Status.Code.UNAVAILABLE, status.code)
  }

  @Test
  fun engineInvalidArgumentPassesThroughUnchanged() {
    val port = fakeEngine { _, o ->
      o.onError(Status.INVALID_ARGUMENT.withDescription("unknown column: bogus").asRuntimeException())
    }
    val status = statusOf { client("localhost:$port").execute(sumRevenue("sales")) }
    assertEquals(Status.Code.INVALID_ARGUMENT, status.code)
    assertEquals("unknown column: bogus", status.description)
  }

  @Test
  fun emptyDatasetIsInvalidArgument() {
    val status = statusOf { client("localhost:1").execute(sumRevenue("")) }
    assertEquals(Status.Code.INVALID_ARGUMENT, status.code)
  }
}
