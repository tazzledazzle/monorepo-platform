package analytics.gateway

import kotlin.system.exitProcess

private const val USAGE = "usage: gateway --engine=<host:port> [--port=<port>]  (--port=0 picks a free port)"

fun main(args: Array<String>) {
  var port = 8080
  var engine: String? = null
  for (arg in args) {
    when {
      arg.startsWith("--port=") ->
          port = arg.removePrefix("--port=").toIntOrNull()?.takeIf { it in 0..65535 } ?: usage("bad --port")
      arg.startsWith("--engine=") -> engine = arg.removePrefix("--engine=")
      else -> usage("unknown argument: $arg")
    }
  }
  if (engine.isNullOrEmpty()) usage("--engine=<host:port> is required")

  val gateway = GatewayServer(port, engine)
  val bound = gateway.start()
  Runtime.getRuntime().addShutdownHook(Thread { gateway.stop() })
  println("LISTENING $bound")
  System.out.flush()
  gateway.awaitTermination()
}

private fun usage(message: String): Nothing {
  System.err.println("gateway: $message\n$USAGE")
  exitProcess(2)
}
