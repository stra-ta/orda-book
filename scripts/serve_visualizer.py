#!/usr/bin/env python3
"""Serve the order-by-order visualizer and replay ad-hoc scenarios through the
real C++ matcher.

The page draws orders and fills that this engine produced. Changing a number in
the browser has to change the match, which means the engine has to run. This
server exists for that and nothing else:

    GET /api/trace?events=<url-encoded event text>

writes the text to a temporary file, runs `orda_replay --trace-json` on it, and
returns the engine's JSON. A rejected event comes back as a frame with
`accepted: false`, so the page can show the refusal instead of an error page.

Dev only. It binds to the loopback interface, it runs one fixed binary with no
shell, and it never touches anything outside its own temporary directory. Do not
expose it and do not deploy it: for a static host, the page falls back to the
checked-in trace and hides the parameter panel.

    python3 scripts/serve_visualizer.py
    python3 scripts/serve_visualizer.py --replay build/orda_replay --port 8000
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
import urllib.parse
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

MAX_EVENT_BYTES = 64 * 1024
MAX_EVENT_LINES = 2000
REPLAY_TIMEOUT_SECONDS = 10

REPO_ROOT = Path(__file__).resolve().parent.parent
VISUALIZER_DIR = REPO_ROOT / "visualizer"


def build_handler(replay: Path):
    class Handler(SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=str(VISUALIZER_DIR), **kwargs)

        def do_GET(self):  # noqa: N802 - name fixed by BaseHTTPRequestHandler
            parsed = urllib.parse.urlparse(self.path)
            if parsed.path != "/api/trace":
                super().do_GET()
                return
            self.handle_trace(parsed)

        def handle_trace(self, parsed):
            params = urllib.parse.parse_qs(parsed.query)
            events = (params.get("events") or [""])[0].encode("utf-8")
            if not events:
                self.send_error_json(400, "No events were supplied.")
                return
            if len(events) > MAX_EVENT_BYTES or events.count(b"\n") > MAX_EVENT_LINES:
                self.send_error_json(
                    413,
                    f"That scenario is too large. Keep it under {MAX_EVENT_BYTES} bytes "
                    f"and {MAX_EVENT_LINES} lines.",
                )
                return

            with tempfile.TemporaryDirectory(prefix="orda-trace-") as work:
                source = Path(work) / "events.txt"
                source.write_bytes(events)
                try:
                    finished = subprocess.run(
                        [str(replay), "--trace-json", str(source)],
                        capture_output=True,
                        timeout=REPLAY_TIMEOUT_SECONDS,
                        shell=False,
                        check=False,
                    )
                except subprocess.TimeoutExpired:
                    self.send_error_json(504, "The matcher did not answer in time.")
                    return

            if finished.returncode != 0:
                self.send_error_json(400, finished.stderr.decode("utf-8", "replace").strip()
                                     or "The matcher rejected that scenario.")
                return
            self.send_json(200, finished.stdout.decode("utf-8", "replace"))

        def send_json(self, status, body):
            payload = body.encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(payload)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(payload)

        def send_error_json(self, status, message):
            self.send_json(status, json.dumps({"error": message}))

        def log_message(self, fmt, *args):
            sys.stderr.write(f"  {self.address_string()} {fmt % args}\n")

    return Handler


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--replay",
        type=Path,
        default=REPO_ROOT / "build-verify" / "orda_replay",
        help="path to orda_replay (default: build-verify/orda_replay)",
    )
    parser.add_argument("--host", default="127.0.0.1", help="loopback by default")
    parser.add_argument("--port", type=int, default=8000)
    args = parser.parse_args()

    if not args.replay.is_file() or not os.access(args.replay, os.X_OK):
        print(f"orda_replay not found or not executable: {args.replay}", file=sys.stderr)
        print("Build it first: cmake -S . -B build-verify && cmake --build build-verify --target orda_replay",
              file=sys.stderr)
        return 1

    server = ThreadingHTTPServer((args.host, args.port), build_handler(args.replay))
    print(f"orda-book visualizer on http://{args.host}:{args.port}")
    print(f"  serving  {VISUALIZER_DIR}")
    print(f"  matcher  {args.replay}")
    print("  dev only, loopback only, ctrl-c to stop")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
