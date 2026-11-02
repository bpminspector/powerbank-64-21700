#!/usr/bin/env python3
import argparse
from collections import deque
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import sys
import threading
from urllib.parse import parse_qs, urlsplit

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "firmware" / "scripts"))
from local_env import read_env

DEFAULT_ENV = Path(__file__).resolve().parents[1] / ".env"
MAX_REQUEST_BYTES = 64 * 1024
MAX_RECORDS_PER_REQUEST = 50
MAX_MEMORY_RECORDS = 5000
MAX_LOG_FILE_BYTES = 5 * 1024 * 1024

records = deque(maxlen=MAX_MEMORY_RECORDS)
records_lock = threading.Lock()
log_path = Path.home() / ".local" / "state" / "powerbank64" / "logs.jsonl"


def load_recent_records() -> None:
    if not log_path.exists():
        return
    with log_path.open("r", encoding="utf-8") as log_file:
        for line in deque(log_file, maxlen=MAX_MEMORY_RECORDS):
            try:
                record = json.loads(line)
            except json.JSONDecodeError:
                continue
            if isinstance(record, dict):
                records.append(record)


def persist_record(record: dict) -> None:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    if log_path.exists() and log_path.stat().st_size >= MAX_LOG_FILE_BYTES:
        rotated = log_path.with_suffix(".jsonl.1")
        rotated.unlink(missing_ok=True)
        log_path.replace(rotated)
    with log_path.open("a", encoding="utf-8") as log_file:
        log_file.write(json.dumps(record, ensure_ascii=False, separators=(",", ":")) + "\n")


class LogRequestHandler(BaseHTTPRequestHandler):
    server_version = "PowerBank64LogServer/1.0"

    def log_message(self, fmt: str, *args) -> None:
        return

    def respond(self, status: int, payload: dict) -> None:
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        parsed = urlsplit(self.path)
        if parsed.path == "/health":
            self.respond(200, {"ok": True, "service": "PowerBank64 log collector"})
            return
        if parsed.path != "/logs":
            self.respond(404, {"error": "not found"})
            return

        query = parse_qs(parsed.query)
        try:
            limit = int(query.get("limit", ["200"])[0])
        except ValueError:
            self.respond(400, {"error": "limit must be an integer"})
            return
        if not 1 <= limit <= 1000:
            self.respond(400, {"error": "limit must be between 1 and 1000"})
            return
        with records_lock:
            result = list(records)[-limit:]
        self.respond(200, {"count": len(result), "logs": result})

    def do_POST(self) -> None:
        if urlsplit(self.path).path != "/logs":
            self.respond(404, {"error": "not found"})
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            self.respond(400, {"error": "invalid Content-Length"})
            return
        if not 0 < length <= MAX_REQUEST_BYTES:
            self.respond(413, {"error": "request body must be between 1 and 65536 bytes"})
            return
        try:
            payload = json.loads(self.rfile.read(length))
        except (json.JSONDecodeError, UnicodeDecodeError):
            self.respond(400, {"error": "body must be valid JSON"})
            return
        if not isinstance(payload, dict) or not isinstance(payload.get("logs"), list):
            self.respond(400, {"error": "expected a JSON object with a logs array"})
            return
        if not 1 <= len(payload["logs"]) <= MAX_RECORDS_PER_REQUEST:
            self.respond(400, {"error": "logs array must contain 1 to 50 entries"})
            return

        accepted = []
        received_at = datetime.now(timezone.utc).isoformat()
        for item in payload["logs"]:
            if not isinstance(item, dict):
                self.respond(400, {"error": "each log entry must be an object"})
                return
            message = item.get("message")
            uptime_ms = item.get("uptime_ms")
            if not isinstance(message, str) or len(message) > 1024:
                self.respond(400, {"error": "message must be a string of at most 1024 characters"})
                return
            if not isinstance(uptime_ms, int) or uptime_ms < 0:
                self.respond(400, {"error": "uptime_ms must be a non-negative integer"})
                return
            accepted.append({
                "received_at": received_at,
                "client_ip": self.client_address[0],
                "device": str(payload.get("device", "unknown"))[:64],
                "uptime_ms": uptime_ms,
                "message": message,
            })

        with records_lock:
            for record in accepted:
                persist_record(record)
                records.append(record)
        self.respond(200, {"accepted": len(accepted)})


def main() -> None:
    parser = argparse.ArgumentParser(description="Receive PowerBank64 ESP32 JSON logs on the local network.")
    parser.add_argument("--env", type=Path, default=DEFAULT_ENV)
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()
    if not 1024 <= args.port <= 65535:
        parser.error("--port must be between 1024 and 65535")

    config = read_env(args.env)
    host = config.get("WIFI_LOCAL_ADMIN_SERVER_IP", "")
    if not host:
        parser.error("WIFI_LOCAL_ADMIN_SERVER_IP is missing from .env")

    load_recent_records()
    server = ThreadingHTTPServer((host, args.port), LogRequestHandler)
    server.daemon_threads = True
    print(f"PowerBank64 JSON logs: http://{host}:{args.port}/logs")
    print("Bound only to the configured LAN address. Press Ctrl+C to stop.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
