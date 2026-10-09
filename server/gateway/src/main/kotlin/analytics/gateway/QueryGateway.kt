package analytics.gateway

import analytics.catalog.Catalog
import com.example.analytics.v1.QueryRequest
import com.example.analytics.v1.QueryResponse
import com.example.analytics.v1.QueryServiceGrpc
import io.grpc.Context
import io.grpc.Deadline
import io.grpc.ManagedChannel
import io.grpc.Status
import io.grpc.StatusRuntimeException
import io.grpc.stub.StreamObserver
import java.util.concurrent.TimeUnit

/** Default per-call engine deadline (docs/proto-compatibility.md: 30 s for Execute). */
const val DEFAULT_ENGINE_DEADLINE_SECONDS = 30L

/**
 * Public QueryService: validates the dataset against the [catalog], then forwards to the engine.
 *
 * Failure mapping (spike tlow-a6j.5.1): unknown dataset is NOT_FOUND; engine statuses pass
 * through unchanged; transport failure or the gateway's own deadline expiring is UNAVAILABLE.
 * No retries.
 */
class QueryGateway(
    private val catalog: Catalog,
    private val engine: ManagedChannel,
    private val engineTarget: String,
    private val engineDeadlineMillis: Long = TimeUnit.SECONDS.toMillis(DEFAULT_ENGINE_DEADLINE_SECONDS),
) : QueryServiceGrpc.QueryServiceImplBase() {

  private val stub = QueryServiceGrpc.newBlockingStub(engine)

  override fun execute(request: QueryRequest, responseObserver: StreamObserver<QueryResponse>) {
    responseObserver.fail(
        try {
          responseObserver.onNext(forward(request))
          responseObserver.onCompleted()
          return
        } catch (e: StatusRuntimeException) {
          map(e)
        } catch (e: RuntimeException) {
          Status.INTERNAL.withDescription("gateway error: ${e.message}")
        })
  }

  private fun forward(request: QueryRequest): QueryResponse {
    if (request.dataset.isEmpty()) {
      throw Status.INVALID_ARGUMENT.withDescription("dataset must be non-empty").asRuntimeException()
    }
    if (!catalog.lookup(request.dataset).isPresent) {
      throw Status.NOT_FOUND.withDescription("unknown dataset: ${request.dataset}").asRuntimeException()
    }
    val own = Deadline.after(engineDeadlineMillis, TimeUnit.MILLISECONDS)
    val incoming: Deadline? = Context.current().deadline
    val effective = if (incoming == null) own else incoming.minimum(own)
    return stub.withDeadline(effective).execute(request)
  }

  private fun map(e: StatusRuntimeException): Status =
      when (e.status.code) {
        Status.Code.UNAVAILABLE ->
            Status.UNAVAILABLE.withDescription("engine unreachable at $engineTarget")
        Status.Code.DEADLINE_EXCEEDED ->
            if (Context.current().deadline?.isExpired == true) {
              Status.DEADLINE_EXCEEDED.withDescription("deadline exceeded")
            } else {
              Status.UNAVAILABLE.withDescription(
                  "engine at $engineTarget did not answer within ${engineDeadlineMillis} ms")
            }
        // NOT_FOUND from the catalog and everything the engine says passes through.
        else -> e.status
      }

  private fun StreamObserver<QueryResponse>.fail(status: Status) {
    onError(status.asRuntimeException())
  }

  fun shutdownEngineChannel() {
    engine.shutdown()
    if (!engine.awaitTermination(2, TimeUnit.SECONDS)) engine.shutdownNow()
  }
}
