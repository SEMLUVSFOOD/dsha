#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# Copyright (c) 2026 DSHA contributors
#
# A tiny fake Home Assistant server implementing the subset of the REST API
# used by DSHA. It's used by the automated tests, and it's handy to try the
# app in an emulator without a real Home Assistant installation:
#
#     python3 tests/fake_ha.py --bind 0.0.0.0 --port 8123
#
# Then use "token=testtoken" in config.ini.

import argparse
import copy
import json
import os
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
TOKEN = "testtoken"


class State:
    def __init__(self, path):
        with open(path, encoding="utf-8") as f:
            self.states = {s["entity_id"]: s for s in json.load(f)}
        self.lock = threading.Lock()

    def get(self, eid):
        with self.lock:
            s = self.states.get(eid)
            return copy.deepcopy(s) if s else None

    def all(self):
        with self.lock:
            return copy.deepcopy(list(self.states.values()))

    def call(self, domain, service, data):
        eid = data.get("entity_id")
        with self.lock:
            s = self.states.get(eid)
            if s is None or not eid.startswith(domain + "."):
                return None
            st, attrs = s["state"], s["attributes"]
            if service == "toggle":
                if domain == "cover":
                    s["state"] = "closed" if st == "open" else "open"
                elif domain == "climate":
                    s["state"] = "off" if st != "off" else "heat"
                else:
                    s["state"] = "off" if st == "on" else "on"
            elif service == "turn_on":
                if domain in ("scene", "script"):
                    return []  # Nothing changes
                s["state"] = "on"
                if "brightness_step_pct" in data:
                    b = attrs.get("brightness") or 0
                    b = max(0, min(255, b + int(2.55 * data["brightness_step_pct"])))
                    attrs["brightness"] = b
                    if b == 0:
                        s["state"] = "off"
            elif service == "turn_off":
                s["state"] = "off"
            elif service in ("lock", "unlock"):
                s["state"] = service + "ed"
            elif service == "open_cover":
                s["state"] = "open"
                attrs["current_position"] = 100
            elif service == "close_cover":
                s["state"] = "closed"
                attrs["current_position"] = 0
            elif service == "set_temperature":
                attrs["temperature"] = data["temperature"]
            elif service == "set_value":
                s["state"] = str(float(data["value"]))
            elif service in ("volume_up", "volume_down"):
                v = attrs.get("volume_level", 0.5)
                v += 0.05 if service == "volume_up" else -0.05
                attrs["volume_level"] = round(max(0, min(1, v)), 2)
            elif service == "media_play_pause":
                s["state"] = "paused" if st == "playing" else "playing"
            elif service == "start":
                s["state"] = "cleaning"
            elif service == "return_to_base":
                s["state"] = "returning"
            elif service == "press":
                s["state"] = "2026-11-16T12:00:00+00:00"
            else:
                return None
            return [copy.deepcopy(s)]


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "FakeHA/1.0"

    def log_message(self, fmt, *args):
        if not self.server.quiet:
            sys.stderr.write("fake_ha: " + (fmt % args) + "\n")

    def send_json(self, code, obj):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        if self.server.mode == "close":
            self.send_header("Connection", "close")
            self.close_connection = True
        if self.server.mode == "chunked":
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            # Send odd chunk sizes, with a chunk extension on the first one
            pos, first = 0, True
            while pos < len(body):
                n = min(1000, len(body) - pos)
                ext = ";foo=bar" if first else ""
                first = False
                self.wfile.write(b"%x%s\r\n" % (n, ext.encode()))
                self.wfile.write(body[pos:pos + n] + b"\r\n")
                pos += n
            self.wfile.write(b"0\r\n\r\n")
        else:
            if self.server.mode != "close":
                self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    def authorized(self):
        if self.headers.get("Authorization") == "Bearer " + TOKEN:
            return True
        body = b"401: Unauthorized"
        self.send_response(401)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
        return False

    def do_GET(self):
        if not self.authorized():
            return
        if self.path == "/api/":
            self.send_json(200, {"message": "API running."})
        elif self.path == "/api/states":
            self.send_json(200, self.server.state.all())
        elif self.path.startswith("/api/states/"):
            s = self.server.state.get(self.path[len("/api/states/"):])
            if s is None:
                self.send_json(404, {"message": "Entity not found."})
            else:
                self.send_json(200, s)
        else:
            self.send_json(404, {"message": "Not found"})

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(length)
        if not self.authorized():
            return
        parts = self.path.split("/")
        if len(parts) != 5 or parts[1] != "api" or parts[2] != "services":
            self.send_json(404, {"message": "Not found"})
            return
        try:
            data = json.loads(raw or b"{}")
        except ValueError:
            self.send_json(400, {"message": "Invalid JSON"})
            return
        result = self.server.state.call(parts[3], parts[4], data)
        if result is None:
            self.send_json(400, {"message": "Service not found."})
        else:
            self.send_json(200, result)


def make_server(bind, port, mode="length", quiet=True, fixture=None):
    httpd = ThreadingHTTPServer((bind, port), Handler)
    httpd.state = State(fixture or os.path.join(HERE, "fixtures", "states.json"))
    httpd.mode = mode
    httpd.quiet = quiet
    httpd.daemon_threads = True
    return httpd


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--bind", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8123)
    ap.add_argument("--mode", choices=["length", "chunked", "close"],
                    default="length", help="How responses are framed")
    ap.add_argument("--fixture", help="JSON file with the states")
    args = ap.parse_args()
    httpd = make_server(args.bind, args.port, args.mode, quiet=False,
                        fixture=args.fixture)
    print(f"Fake Home Assistant on http://{args.bind}:{args.port} "
          f"(token: {TOKEN})")
    httpd.serve_forever()


if __name__ == "__main__":
    main()
