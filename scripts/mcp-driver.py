"""mcp-driver.py — drive the embedded 3dmovie MCP server from a script.

Spawns 3dmovie.exe --mcp-server, sends a sequence of tool calls, prints text
responses, and saves image responses (PNG screenshots) to %TEMP% so the
parent shell can `start` them.

Usage:
  py scripts/mcp-driver.py [--exe PATH] [--quit-on-exit] < script.txt

Script syntax (one tool call per line):
  tool_name key=value key2=value
  # comment line
  (blank lines allowed)

Examples:
  get_state
  wait_ms ms=2000
  click x=320 y=240 button=left
  send_command cid=0x1234
  screenshot
  read_crash_log
  quit

Values are parsed as int if they parse cleanly (incl. 0x prefix), bool if
'true'/'false', else string.
"""
from __future__ import annotations

import argparse
import json
import os
import shlex
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def coerce(v: str):
    if v.lower() == "true": return True
    if v.lower() == "false": return False
    try:
        return int(v, 0)
    except ValueError:
        pass
    try:
        return float(v)
    except ValueError:
        pass
    return v


def parse_line(line: str):
    parts = shlex.split(line)
    if not parts:
        return None, {}
    tool = parts[0]
    args = {}
    for p in parts[1:]:
        if "=" not in p:
            raise ValueError(f"bad arg {p!r}, expected key=value")
        k, _, v = p.partition("=")
        args[k] = coerce(v)
    return tool, args


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=r"C:\Users\wjbr\src\3DMMForever\dist-x64\3dmovie.exe")
    ap.add_argument("--quit-on-exit", action="store_true",
                    help="Send quit at end of script. Default leaves the app running.")
    ap.add_argument("--launch-wait", type=float, default=3.0,
                    help="Seconds to wait after launching for studio to settle before sending requests.")
    args = ap.parse_args()

    exe = Path(args.exe)
    if not exe.exists():
        print(f"exe not found: {exe}", file=sys.stderr)
        return 2

    # Spawn child with text pipes. utf-8 explicitly so we don't accidentally
    # send a BOM (the server tolerates it but no need).
    proc = subprocess.Popen(
        [str(exe), "--mcp-server"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, encoding="utf-8", bufsize=1,
    )

    next_id = 1

    def send(method: str, params=None, *, notification=False):
        nonlocal next_id
        msg = {"jsonrpc": "2.0", "method": method}
        if not notification:
            msg["id"] = next_id
            next_id += 1
        if params is not None:
            msg["params"] = params
        line = json.dumps(msg)
        proc.stdin.write(line + "\n")
        proc.stdin.flush()
        if notification:
            return None
        return read_response(msg["id"])

    def read_response(want_id: int, timeout: float = 30.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = proc.stdout.readline()
            if not line:
                return {"error": "stdout closed (server may have crashed)"}
            try:
                obj = json.loads(line)
            except Exception as e:
                print(f"# unparseable from server: {line!r}", file=sys.stderr)
                continue
            if obj.get("id") == want_id:
                return obj
        return {"error": "timeout"}

    # Wait for studio init (window creation, splash) before talking.
    time.sleep(args.launch_wait)

    init = send("initialize", {
        "protocolVersion": "2024-11-05",
        "capabilities": {},
        "clientInfo": {"name": "mcp-driver", "version": "0.1"},
    })
    if "error" in init:
        print(f"initialize failed: {init['error']}")
        proc.kill()
        return 1
    send("notifications/initialized", notification=True)

    out_dir = Path(tempfile.gettempdir()) / "3dmm-mcp"
    out_dir.mkdir(exist_ok=True)

    src = sys.stdin
    line_no = 0
    try:
        for raw in src:
            line_no += 1
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            try:
                tool, targs = parse_line(line)
            except Exception as e:
                print(f"line {line_no}: {e}", file=sys.stderr)
                continue
            if not tool:
                continue
            print(f"\n>>> {tool} {targs}")
            resp = send("tools/call", {"name": tool, "arguments": targs})
            if "error" in resp:
                print(f"!!! error: {resp['error']}")
                continue
            result = resp.get("result", {})
            for item in result.get("content", []):
                t = item.get("type")
                if t == "text":
                    print(item.get("text", ""))
                elif t == "image" and item.get("mimeType") == "image/png":
                    import base64
                    data = base64.b64decode(item["data"])
                    ts = time.strftime("%H%M%S")
                    out = out_dir / f"shot-{ts}-{tool}-{line_no}.png"
                    out.write_bytes(data)
                    print(f"<image saved: {out} ({len(data)} bytes)>")
                else:
                    print(f"<unknown content: {item}>")
            if result.get("isError"):
                print("!!! tool reported isError=true")
    finally:
        if args.quit_on_exit and proc.poll() is None:
            try:
                send("tools/call", {"name": "quit", "arguments": {}})
                time.sleep(1)
            except Exception:
                pass
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()

    return 0


if __name__ == "__main__":
    sys.exit(main())
