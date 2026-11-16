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
# Then use "token=testtoken" in config.ini, or sign in with user "sam" and
# password "secret" (user "mfa" also asks for the two-factor code 123456).

import argparse
import copy
import json
import os
import sys
import secrets
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
TOKEN = "testtoken"
# Users for the login flow. "mfa" also needs the two-factor code 123456.
USERS = {"sam": "secret", "mfa": "secret"}
MFA_CODE = "123456"


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


class Auth:
    """Login flow and tokens, like homeassistant/components/auth."""

    def __init__(self):
        self.flows = {}     # flow_id -> {"client_id", "step", "user"}
        self.codes = {}     # authorization code -> (client_id, user)
        self.refresh = {}   # refresh token -> client_id
        self.access = set()
        self.lock = threading.Lock()

    @staticmethod
    def same_origin(a, b):
        from urllib.parse import urlparse
        pa, pb = urlparse(a), urlparse(b)
        return pa.scheme == pb.scheme and pa.netloc == pb.netloc

    def new_access(self):
        tok = "acc-" + secrets.token_hex(40)
        self.access.add(tok)
        return tok


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
        auth = self.headers.get("Authorization", "")
        if auth == "Bearer " + TOKEN:
            return True
        if auth.startswith("Bearer ") and auth[7:] in self.server.auth.access:
            return True
        body = b"401: Unauthorized"
        self.send_response(401)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
        return False

    def do_GET(self):
        if self.path == "/auth/providers":
            self.send_json(200, {"providers": [
                {"name": "Home Assistant Local", "id": None,
                 "type": "homeassistant"}], "preselect_remember_me": False})
            return
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

    def auth_post(self, raw):
        auth = self.server.auth
        if self.path == "/auth/login_flow":
            data = json.loads(raw)
            if (not data.get("client_id") or not data.get("redirect_uri")
                    or not isinstance(data.get("handler"), list)):
                self.send_json(400, {"message": "Invalid data"})
                return
            if not Auth.same_origin(data["client_id"], data["redirect_uri"]):
                self.send_json(403, {"message": "Invalid redirect URI"})
                return
            flow_id = secrets.token_hex(16)
            with auth.lock:
                auth.flows[flow_id] = {"client_id": data["client_id"],
                                       "step": "init", "user": None}
            self.send_json(200, {"type": "form", "flow_id": flow_id,
                                 "handler": data["handler"],
                                 "step_id": "init", "data_schema": [],
                                 "errors": {}})
            return

        if self.path.startswith("/auth/login_flow/"):
            data = json.loads(raw)
            flow_id = self.path.rsplit("/", 1)[1]
            with auth.lock:
                flow = auth.flows.get(flow_id)
            if flow is None:
                self.send_json(404, {"message": "Invalid flow specified"})
                return
            if data.get("client_id") != flow["client_id"]:
                self.send_json(400, {"message": "Invalid client id"})
                return
            base = {"flow_id": flow_id, "handler": ["homeassistant", None],
                    "data_schema": []}
            if flow["step"] == "init":
                user = data.get("username")
                if USERS.get(user) != data.get("password"):
                    self.send_json(200, dict(base, type="form", step_id="init",
                                             errors={"base": "invalid_auth"}))
                    return
                flow["user"] = user
                if user == "mfa":
                    flow["step"] = "mfa"
                    self.send_json(200, dict(base, type="form", step_id="mfa",
                                             errors={}))
                    return
            elif flow["step"] == "mfa":
                if data.get("code") != MFA_CODE:
                    self.send_json(200, dict(base, type="form", step_id="mfa",
                                             errors={"base": "invalid_code"}))
                    return
            code = secrets.token_hex(16)
            with auth.lock:
                auth.codes[code] = (flow["client_id"], flow["user"])
                del auth.flows[flow_id]
            self.send_json(200, dict(base, type="create_entry", version=1,
                                     result=code, title=flow["user"]))
            return

        if self.path == "/auth/token":
            from urllib.parse import parse_qs
            form = {k: v[0] for k, v in parse_qs(raw.decode()).items()}
            grant = form.get("grant_type")
            with auth.lock:
                if grant == "authorization_code":
                    entry = auth.codes.pop(form.get("code"), None)
                    if entry is None or entry[0] != form.get("client_id"):
                        self.send_json(400, {"error": "invalid_request",
                                             "error_description": "Invalid code"})
                        return
                    refresh = "ref-" + secrets.token_hex(32)
                    auth.refresh[refresh] = entry[0]
                    self.send_json(200, {"access_token": auth.new_access(),
                                         "expires_in": 1800,
                                         "refresh_token": refresh,
                                         "token_type": "Bearer"})
                    return
                if grant == "refresh_token":
                    client = auth.refresh.get(form.get("refresh_token"))
                    if client is None or client != form.get("client_id"):
                        self.send_json(400, {"error": "invalid_grant"})
                        return
                    self.send_json(200, {"access_token": auth.new_access(),
                                         "expires_in": 1800,
                                         "token_type": "Bearer"})
                    return
            self.send_json(400, {"error": "unsupported_grant_type"})
            return

        # Test hooks
        if self.path == "/test/expire_access_tokens":
            with auth.lock:
                auth.access.clear()
            self.send_json(200, {})
            return
        if self.path == "/test/revoke_refresh_tokens":
            with auth.lock:
                auth.refresh.clear()
                auth.access.clear()
            self.send_json(200, {})
            return
        self.send_json(404, {"message": "Not found"})

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(length)
        if self.path.startswith("/auth/") or self.path.startswith("/test/"):
            self.auth_post(raw)
            return
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
    httpd.auth = Auth()
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
