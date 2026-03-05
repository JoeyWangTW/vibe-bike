#!/usr/bin/env python3
"""stats_server.py — Serve real-time Claude Code stats to the Vibe Bike ESP32.

Scans ~/.claude/projects/ session JSONL files for today's usage data and serves
live stats over HTTP. The ESP32 polls this endpoint every 30s to show Claude Code
activity on the dashboard.

Usage:
    python3 scripts/stats_server.py              # Defaults to port 8888
    python3 scripts/stats_server.py --port 9999  # Custom port

The ESP32 fetches:  GET http://<your-mac-ip>:8888/stats
Response:
    {
      "date": "2026-02-27",
      "messages": 142,
      "sessions": 5,
      "toolCalls": 38,
      "outputTokens": 12345
    }
"""

import argparse
import json
import socket
from datetime import date, timezone
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

CLAUDE_DIR = Path.home() / ".claude"
PROJECTS_DIR = CLAUDE_DIR / "projects"


def get_today_stats():
    """Scan all session JSONL files for today's usage data (real-time)."""
    today = date.today().isoformat()
    today_prefix = today + "T"  # e.g. "2026-02-27T"

    messages = 0
    tool_calls = 0
    output_tokens = 0
    session_ids = set()

    if not PROJECTS_DIR.exists():
        return {"date": today, "messages": 0, "sessions": 0, "toolCalls": 0, "outputTokens": 0}

    # Scan all project directories
    for project_dir in PROJECTS_DIR.iterdir():
        if not project_dir.is_dir():
            continue

        # Check each JSONL file in the project
        for jsonl_file in project_dir.glob("*.jsonl"):
            # Quick check: skip files not modified today (optimization)
            try:
                from datetime import datetime
                mtime = datetime.fromtimestamp(jsonl_file.stat().st_mtime, tz=timezone.utc)
                if mtime.date().isoformat() < today:
                    continue
            except Exception:
                pass

            try:
                with open(jsonl_file) as f:
                    for line in f:
                        line = line.strip()
                        if not line:
                            continue

                        # Quick string check before parsing JSON (performance)
                        if today_prefix not in line:
                            continue

                        try:
                            entry = json.loads(line)
                        except json.JSONDecodeError:
                            continue

                        ts = entry.get("timestamp", "")
                        if not ts.startswith(today_prefix):
                            continue

                        entry_type = entry.get("type", "")
                        session_id = entry.get("sessionId", "")

                        if entry_type == "assistant":
                            msg = entry.get("message", {})
                            usage = msg.get("usage", {})
                            out_tok = usage.get("output_tokens", 0)
                            output_tokens += out_tok
                            messages += 1
                            if session_id:
                                session_ids.add(session_id)

                            # Count tool use in assistant messages
                            content = msg.get("content", [])
                            if isinstance(content, list):
                                for block in content:
                                    if isinstance(block, dict) and block.get("type") == "tool_use":
                                        tool_calls += 1

                        elif entry_type == "user":
                            messages += 1
                            if session_id:
                                session_ids.add(session_id)

            except Exception as e:
                # Skip files we can't read
                continue

    return {
        "date": today,
        "messages": messages,
        "sessions": len(session_ids),
        "toolCalls": tool_calls,
        "outputTokens": output_tokens,
    }


class StatsHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/stats" or self.path == "/":
            stats = get_today_stats()
            body = json.dumps(stats).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(body)
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, format, *args):
        print(f"[{self.log_date_time_string()}] {args[0]}")


def get_local_ip():
    """Get the machine's local network IP address."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"


def main():
    parser = argparse.ArgumentParser(description="Serve Claude Code stats to ESP32")
    parser.add_argument("--port", type=int, default=8888, help="Port to listen on (default: 8888)")
    args = parser.parse_args()

    local_ip = get_local_ip()
    server = HTTPServer(("0.0.0.0", args.port), StatsHandler)

    print(f"Claude Code Stats Server (real-time)")
    print(f"  Scanning: {PROJECTS_DIR}/*/**.jsonl")
    print(f"  Serving:  http://{local_ip}:{args.port}/stats")
    print(f"")
    print(f"  Set in config.h:")
    print(f'    #define STATS_SERVER_IP   "{local_ip}"')
    print(f"    #define STATS_SERVER_PORT  {args.port}")
    print(f"")
    print(f"Press Ctrl+C to stop.")

    # Show current stats
    stats = get_today_stats()
    print(f"\nToday's stats ({stats['date']}):")
    print(f"  Messages:      {stats['messages']}")
    print(f"  Sessions:      {stats['sessions']}")
    print(f"  Tool calls:    {stats['toolCalls']}")
    print(f"  Output tokens: {stats['outputTokens']}")
    print()

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")
        server.server_close()


if __name__ == "__main__":
    main()
