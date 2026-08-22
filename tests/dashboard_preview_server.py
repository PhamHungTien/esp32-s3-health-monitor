#!/usr/bin/env python3
"""Phục vụ dashboard nhúng với dữ liệu mẫu để kiểm tra bố cục cục bộ."""

from __future__ import annotations

import json
import re
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "donghothongminh" / "web_dashboard.h"
MATCH = re.search(r'R"HTML\((.*)\)HTML";', HEADER.read_text(), re.DOTALL)
if MATCH is None:
    raise RuntimeError("Không tìm thấy WEB_DASHBOARD_HTML")
HTML = MATCH.group(1).encode()

STATUS = {
    "fingerPresent": True,
    "heartRateValid": True,
    "spo2Valid": True,
    "bpm": 76.0,
    "spo2": 97.0,
    "signalQuality": 86.0,
    "redRaw": 129840,
    "irRaw": 134822,
    "steps": 1284,
    "fall": "OK",
    "acceleration": 1.02,
    "oledOK": True,
    "imuOK": True,
    "maxOK": True,
    "buzzerOK": True,
    "gpsState": "FIX",
    "satellites": 7,
    "positionKnown": True,
    "latitude": 10.762622,
    "longitude": 106.660172,
    "uptime": 428,
    "clients": 1,
}


class Handler(BaseHTTPRequestHandler):
    def send_bytes(self, body: bytes, content_type: str) -> None:
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        if self.path.startswith("/api/status"):
            self.send_bytes(json.dumps(STATUS).encode(), "application/json")
        else:
            self.send_bytes(HTML, "text/html; charset=utf-8")

    def do_POST(self) -> None:
        self.send_bytes(b'{"ok":true}', "application/json")

    def log_message(self, format: str, *args: object) -> None:
        pass


if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", 8765), Handler).serve_forever()
