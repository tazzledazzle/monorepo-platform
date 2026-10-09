// Engine server. Flags: --port=N (default 0: pick a free port), --host=ADDR
// (default 127.0.0.1). Once the server accepts RPCs it prints exactly
// "LISTENING <port>\n" on stdout and flushes; SIGTERM/SIGINT shut it down
// cleanly with exit code 0.
#include <pthread.h>
#include <signal.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "engine/service/engine_service.h"

namespace {

bool ParseFlag(const std::string& arg, const char* name, std::string* value) {
  const std::string prefix = std::string("--") + name + "=";
  if (arg.rfind(prefix, 0) != 0) return false;
  *value = arg.substr(prefix.size());
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  int port = 0;
  std::string host = "127.0.0.1";
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    std::string v;
    if (ParseFlag(arg, "port", &v)) {
      char* end = nullptr;
      long p = std::strtol(v.c_str(), &end, 10);
      if (v.empty() || *end != '\0' || p < 0 || p > 65535) {
        std::fprintf(stderr, "invalid --port: %s\n", v.c_str());
        return 2;
      }
      port = static_cast<int>(p);
    } else if (ParseFlag(arg, "host", &v)) {
      host = v;
    } else {
      std::fprintf(stderr, "usage: %s [--port=N] [--host=ADDR]\n", argv[0]);
      return 2;
    }
  }

  // Block the signals before any thread exists so every gRPC thread inherits
  // the mask and sigwait below is the only receiver (no async-signal handler).
  sigset_t signals;
  sigemptyset(&signals);
  sigaddset(&signals, SIGTERM);
  sigaddset(&signals, SIGINT);
  pthread_sigmask(SIG_BLOCK, &signals, nullptr);

  std::string error;
  auto server = analytics::engine::EngineServer::Start(host, port, &error);
  if (!server) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
  std::printf("LISTENING %d\n", server->port());
  std::fflush(stdout);

  int sig = 0;
  sigwait(&signals, &sig);
  server->Shutdown();
  return 0;
}
