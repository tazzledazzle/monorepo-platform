package analytics.gateway

import analytics.catalog.Catalog
import io.grpc.Server
import io.grpc.netty.NettyChannelBuilder
import io.grpc.netty.NettyServerBuilder
import java.util.concurrent.TimeUnit

/** Wires the public listener to a [QueryGateway] that talks to `engineTarget` (host:port). */
class GatewayServer(
    private val port: Int,
    private val engineTarget: String,
    catalog: Catalog = Catalog.seeded(),
    engineDeadlineMillis: Long = TimeUnit.SECONDS.toMillis(DEFAULT_ENGINE_DEADLINE_SECONDS),
) {
  // Channel creation is lazy: an unreachable engine does not stop the gateway from starting.
  private val service =
      QueryGateway(
          catalog,
          NettyChannelBuilder.forTarget(engineTarget).usePlaintext().build(),
          engineTarget,
          engineDeadlineMillis)
  private val server: Server = NettyServerBuilder.forPort(port).addService(service).build()

  /** Starts listening and returns the bound port (useful when `port` is 0). */
  fun start(): Int {
    server.start()
    return server.port
  }

  fun awaitTermination() = server.awaitTermination()

  fun stop() {
    server.shutdown()
    if (!server.awaitTermination(5, TimeUnit.SECONDS)) server.shutdownNow()
    service.shutdownEngineChannel()
  }
}
