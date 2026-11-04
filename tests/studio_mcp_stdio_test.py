"""Exercise the actual Studio JSON-RPC stream and graceful shutdown.

Uses a fresh process without opening or editing a movie. Requires a Debug SDL
Studio executable and its original content directory. Works without Win32 APIs.
"""
import argparse
import base64
import json
import os
import pathlib
import queue
import subprocess
import threading
import time
from mcp_png_test import decode_png


def run(executable, content, disconnect, timeout=25):
    process = subprocess.Popen(
        [str(executable.resolve()), "--mcp-server"], cwd=content.resolve(),
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        env={**os.environ, "KAUAI_TEST_FAIL_FAST": "1"},
    )
    replies, errors = queue.Queue(), []

    def read():
        try:
            for line in process.stdout:
                replies.put(json.loads(line))
        except Exception as error:
            replies.put(error)

    def read_errors():
        errors.append(process.stderr.read().decode("utf-8", errors="replace"))

    readers = [threading.Thread(target=read, daemon=True), threading.Thread(target=read_errors, daemon=True)]
    for reader in readers:
        reader.start()

    def receive(expected_id):
        deadline = time.monotonic() + timeout
        while True:
            try:
                reply = replies.get(timeout=min(1, max(0, deadline - time.monotonic())))
                break
            except queue.Empty:
                if process.poll() is not None:
                    raise RuntimeError(f"Studio exited with code {process.returncode} while waiting for {expected_id!r}")
                if time.monotonic() >= deadline:
                    raise TimeoutError(f"Studio did not answer {expected_id!r}")
        if isinstance(reply, Exception):
            raise reply
        assert reply["id"] == expected_id, reply
        assert reply["jsonrpc"] == "2.0", reply
        return reply

    def request(method, identity, params=None):
        return json.dumps({"jsonrpc": "2.0", "id": identity, "method": method, "params": params or {}},
                          ensure_ascii=False).encode("utf-8") + b"\n"

    try:
        message = request("initialize", "caf\u00e9-\U0001f3ac")
        # Split inside a multi-byte UTF-8 sequence as well as before the newline.
        split = message.index(b"\xc3") + 1
        process.stdin.write(message[:split])
        process.stdin.flush()
        process.stdin.write(message[split:-1])
        process.stdin.flush()
        process.stdin.write(b"\r\n")
        process.stdin.flush()
        assert receive("caf\u00e9-\U0001f3ac")["result"]["serverInfo"]["name"] == "3dmovie"

        # Neither a regular notification nor a known-method notification may
        # produce a reply. The next response must belong to the subsequent ping.
        process.stdin.write(b'{"jsonrpc":"2.0","method":"notifications/initialized"}\n'
                            b'{"jsonrpc":"2.0","method":"ping"}\n')
        large_id = "fragment-" + "x" * 10000
        process.stdin.write(request("ping", large_id) + request("tools/list", 2))
        process.stdin.flush()
        assert receive(large_id)["result"] == {}
        tools = {tool["name"] for tool in receive(2)["result"]["tools"]}
        assert {"screenshot", "get_state", "get_sdl_dialog", "sdl_dialog_action", "click", "key", "quit"} <= tools
        if os.name != "nt":
            assert "list_dialogs" not in tools and "dismiss_dialog" not in tools

        process.stdin.write(b"{broken json}\n" + request("unknown-method", 3))
        process.stdin.flush()
        assert receive(None)["error"]["code"] == -32700
        assert receive(3)["error"]["code"] == -32601

        process.stdin.write(request("tools/call", 4, {"name": "screenshot"}))
        process.stdin.flush()
        image = receive(4)["result"]["content"][0]
        assert image["type"] == "image" and image["mimeType"] == "image/png"
        (width, height), _ = decode_png(base64.b64decode(image["data"], validate=True))
        assert width > 0 and height > 0

        if disconnect:
            # A batch spans reader chunks and GUI queue snapshots. Every final
            # request must be answered before EOF schedules Studio's quit.
            identities = [f"before-eof-{index}" for index in range(128)]
            process.stdin.write(b"".join(request("ping", identity) for identity in identities))
            process.stdin.close()
            for identity in identities:
                assert receive(identity)["result"] == {}
        else:
            process.stdin.write(request("tools/call", 5, {"name": "quit"}))
            process.stdin.flush()
            assert "error" not in receive(5)
            # Deliberately leave stdin open: joining the reader must not hang.
        assert process.wait(timeout=15) == 0, "Studio returned failure"
        for reader in readers:
            reader.join(timeout=2)
        assert replies.empty(), "Unexpected extra JSON-RPC replies"
    finally:
        if process.poll() is None:
            process.kill()  # Only this test's newly launched process.
        process.wait(timeout=5)
        for reader in readers:
            reader.join(timeout=2)
        if process.stdin and not process.stdin.closed:
            process.stdin.close()
        if errors and errors[0]:
            print(errors[0])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=pathlib.Path)
    parser.add_argument("content", type=pathlib.Path)
    parser.add_argument("--timeout", type=float, default=25, help="Response timeout in seconds, including startup")
    args = parser.parse_args()
    run(args.exe, args.content, disconnect=False, timeout=args.timeout)
    run(args.exe, args.content, disconnect=True, timeout=args.timeout)
    print("Studio MCP: fragmented UTF-8/CRLF, large requests, notifications, errors, PNG, quit and EOF passed")


if __name__ == "__main__":
    main()
