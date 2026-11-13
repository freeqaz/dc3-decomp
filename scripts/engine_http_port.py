"""A private HTTP debug-server port for a dc3-native a harness launches.

The engine picks the port itself (DC3_HTTP_PORT=0: the kernel assigns a free
one at bind time) and writes it to DC3_HTTP_PORT_FILE once bound
(native/src/platform/HttpServer.cpp).  A harness must not choose a port and
pass it in: between its probe (bind 0, read, close) and the engine's bind --
seconds of boot -- any process on the box can take the port, and under
several lanes' test runs one did (GestureMenusTest, port 48041).  Since
2026-11-13 the engine also refuses to share a port (no SO_REUSEPORT) and exits
98 naming it, so a collision is loud instead of two engines splitting the
requests.

    env.update(engine_port_env(workdir))         # before launching
    port = wait_for_port(port_file(workdir), proc, timeout=120)

Exit 98 from the engine means its port could not be bound (only possible
with an explicit DC3_HTTP_PORT); wait_for_port raises EnginePortError naming it.
"""
import os
import time

PORT_IN_USE_EXIT = 98  # HttpServer.cpp kHttpBindExitCode


class EnginePortError(RuntimeError):
    pass


def port_file(workdir):
    return os.path.join(os.path.abspath(workdir), "http_port")


def engine_port_env(workdir, port=0):
    """Environment for a dc3-native with the debug server on a private port.
    `port` != 0 pins it (a person's fixed port); 0 = kernel-assigned."""
    path = port_file(workdir)
    try:
        os.unlink(path)  # a stale file from an earlier run in this dir
    except FileNotFoundError:
        pass
    return {"DC3_HTTP": "1", "DC3_HTTP_PORT": str(port), "DC3_HTTP_PORT_FILE": path}


def read_port(path):
    try:
        with open(path) as f:
            text = f.read().strip()
    except OSError:
        return None
    return int(text) if text.isdigit() and int(text) > 0 else None


def wait_for_port(path, proc=None, timeout=120.0, alive=None):
    """The port the engine wrote to `path`.  Bounded by `timeout`; stops early
    when `proc` (a Popen) exits or `alive()` turns false, naming exit 98."""
    end = time.monotonic() + timeout
    while True:
        port = read_port(path)
        if port:
            return port
        rc = proc.poll() if proc is not None else None
        if rc is not None:
            if rc == PORT_IN_USE_EXIT:
                raise EnginePortError(
                    "dc3-native exited 98: its HTTP port could not be bound "
                    "(DC3_HTTP_PORT); see the FATAL line in its log")
            raise EnginePortError(f"dc3-native exited ({rc}) before reporting its HTTP port")
        if alive is not None and not alive():
            raise EnginePortError("stopped waiting for the engine's HTTP port")
        if time.monotonic() > end:
            raise EnginePortError(f"dc3-native wrote no HTTP port to {path} in {timeout:.0f} s")
        time.sleep(0.1)
