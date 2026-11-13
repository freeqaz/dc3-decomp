// A private HTTP debug-server port for a dc3-native that a test launches from
// a shell (np-harness).
//
// The engine binds the port itself: DC3_HTTP_PORT=0 lets the kernel pick a
// free one at bind time, and DC3_HTTP_PORT_FILE makes the engine write it to a
// file in the test's run dir once bound (native/src/platform/HttpServer.cpp).
// A test must not pick a port and pass it in.  Between its probe (bind, close)
// and the engine's bind, seconds of boot later, any process on the box can take
// the port, and with several lanes running the gate at once one did
// (GestureMenusTest, port 48041).  Worse, before 2026-11-13 the engine bound
// with SO_REUSEPORT, so a second engine on the same port did NOT fail: both
// listened and the kernel split the requests between them.  The Python
// counterpart is scripts/engine_http_port.py.
//
// Use, inside the test's shell command:
//     ( ... <EngineHttpPortShellEnv(R)> ... exec ./dc3-native ... ) & epid=$!;
//     <EngineHttpApiShell(R, "epid", 120)>        # sets $api, or $api=""
// and give a synthetic sensor `--perform-port-file <EngineHttpPortFile(R)>`.
#pragma once

#include <string>

inline std::string EngineHttpPortFile(const std::string &runDir) {
    return runDir + "/http_port";
}

// Environment assignments (leading space) for the engine's shell command.
inline std::string EngineHttpPortShellEnv(const std::string &runDir) {
    return " DC3_HTTP=1 DC3_HTTP_PORT=0 DC3_HTTP_PORT_FILE=" + EngineHttpPortFile(runDir);
}

// Shell statements: wait up to `seconds` for the engine whose pid is in
// $`pidVar` to write its port, then set $api to http://127.0.0.1:<port>/api.
// If it never does, $api is empty and a `DRIVER:` line says why (exit 98 is
// the engine's "my port could not be bound").
inline std::string EngineHttpApiShell(const std::string &runDir, const char *pidVar,
                                      int seconds) {
    std::string f = EngineHttpPortFile(runDir);
    std::string pid = std::string("$") + pidVar;
    return "end=$((SECONDS+" + std::to_string(seconds) + ")); "
           "until [ -s " + f + " ] || [ $SECONDS -ge $end ] || ! kill -0 " + pid +
           " 2>/dev/null; do sleep 0.2; done; "
           "if [ -s " + f + " ]; then api=http://127.0.0.1:$(cat " + f + ")/api; "
           "echo \"DRIVER: engine HTTP port $(cat " + f + ")\"; "
           "else api=; echo \"DRIVER: engine never reported its HTTP port (alive=$(kill -0 " +
           pid + " 2>/dev/null && echo 1 || echo 0))\"; fi; ";
}
