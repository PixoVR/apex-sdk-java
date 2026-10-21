#!/usr/bin/env python3
"""A fake Apex platform for rehearsing the smoke test without a network.

It serves the responses in spec/fixtures and checks every request the core
sends: paths, auth headers, login and Quick ID bodies, the session event
bodies (uuid, eventType, moduleId only on the join, base64 data), heartbeats
and telemetry packets.

    fake_apex.py serve                  # print the URLs and serve until Ctrl+C
    fake_apex.py run -- COMMAND...      # run the smoke test against it

`run` appends the options that point the smoke test at the fake (environment,
hosts, login, module, telemetry URL) to COMMAND, then exits non-zero if the
command failed or the fake saw a request it did not expect.
"""

import argparse
import base64
import json
import os
import re
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit

FIXTURES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "spec", "fixtures")

MODULE_ID = 42
USERNAME = "smoke"
PASSWORD = "smoke-password"
DATA = b"apex-smoke"
UUID = re.compile(r"^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$")
EVENT_TYPES = ("PIXOVR_SESSION_JOINED", "PIXOVR_SESSION_EVENT", "PIXOVR_SESSION_COMPLETE")


def fixture(name):
    with open(os.path.join(FIXTURES, name), encoding="utf-8") as f:
        return json.load(f)


class Platform:
    """What the fake has seen, and the checks on it."""

    def __init__(self):
        self.lock = threading.Lock()
        self.xapi = False  # set by --xapi: expect xAPI session statements
        self.problems = []
        self.events = []
        self.uuid = None
        self.heartbeats = 0
        self.telemetry = []
        self.tokens = {fixture("login_response.json")["Token"], fixture("quick_id_login_response.json")["Token"]}
        self.session_id = fixture("session_join_response.json")["sessionId"]

    def problem(self, text):
        with self.lock:
            self.problems.append(text)

    def check_event(self, body):
        if self.xapi:
            return self.check_xapi_event(body)
        event_type = body.get("eventType")
        if event_type not in EVENT_TYPES:
            return "unknown eventType %r" % event_type
        if not isinstance(body.get("uuid"), str) or not UUID.match(body["uuid"]):
            return "uuid %r is not a lowercase GUID v4" % body.get("uuid")
        allowed = {"uuid", "eventType", "data"} | ({"moduleId"} if event_type == EVENT_TYPES[0] else set())
        extra = set(body) - allowed
        if extra:
            return "%s carries unexpected fields %s" % (event_type, sorted(extra))
        if event_type == EVENT_TYPES[0] and body.get("moduleId") != MODULE_ID:
            return "join moduleId is %r, expected %d" % (body.get("moduleId"), MODULE_ID)
        if "data" in body:
            try:
                data = base64.b64decode(body["data"], validate=True)
            except (ValueError, TypeError):
                return "data is not base64: %r" % body["data"]
            if data != DATA:
                return "data decodes to %r, expected %r" % (data, DATA)
        with self.lock:
            expected = EVENT_TYPES[len(self.events) % 3]
            if event_type != expected:
                return "got %s, expected %s next" % (event_type, expected)
            if event_type == EVENT_TYPES[0]:
                self.uuid = body["uuid"]
            elif body["uuid"] != self.uuid:
                return "%s uuid differs from the join's" % event_type
            self.events.append(event_type)
        return None

    def check_xapi_event(self, body):
        event_type = body.get("eventType")
        if event_type not in EVENT_TYPES:
            return "unknown eventType %r" % event_type
        if not isinstance(body.get("uuid"), str) or not UUID.match(body["uuid"]):
            return "uuid %r is not a lowercase GUID v4" % body.get("uuid")
        allowed = {"uuid", "eventType", "moduleId", "deviceId", "jsonData"}
        if event_type == EVENT_TYPES[0]:
            allowed.add("ipAddress")
        extra = set(body) - allowed
        if extra:
            return "%s carries unexpected fields %s" % (event_type, sorted(extra))
        if body.get("moduleId") != MODULE_ID:
            return "moduleId is %r, expected %d" % (body.get("moduleId"), MODULE_ID)
        statement = body.get("jsonData")
        if not isinstance(statement, dict):
            return "jsonData is not an xAPI statement"
        for key in ("actor", "verb", "object", "context", "timestamp", "id", "version"):
            if key not in statement:
                return "the statement has no %r" % key
        if statement["context"].get("registration") != body["uuid"]:
            return "context.registration differs from uuid"
        if not statement["actor"].get("mbox"):
            return "the statement's actor has no mbox"
        extension = "https://pixovr.com/xapi/extension/smoke"
        extensions = statement.get("result", {}).get("extensions", {}) if event_type == EVENT_TYPES[2] \
            else statement["context"].get("extensions", {})
        if extensions.get(extension) != DATA.decode("ascii"):
            return "the statement does not carry --data in %s" % extension
        if event_type == EVENT_TYPES[2] and statement.get("lessonStatus") != "passed":
            return "the completion's lessonStatus is %r" % statement.get("lessonStatus")
        with self.lock:
            expected = EVENT_TYPES[len(self.events) % 3]
            if event_type != expected:
                return "got %s, expected %s next" % (event_type, expected)
            if event_type == EVENT_TYPES[0]:
                self.uuid = body["uuid"]
            elif body["uuid"] != self.uuid:
                return "%s uuid differs from the join's" % event_type
            self.events.append(event_type)
        return None

    def check_telemetry(self, packet):
        for key in ("v", "seq", "t0", "s", "u", "m", "ob", "ev"):
            if key not in packet:
                return "telemetry packet has no %r" % key
        with self.lock:
            if self.uuid is not None and packet.get("r") != self.uuid.replace("-", ""):
                return "telemetry r is %r, expected the session uuid without dashes" % packet.get("r")
            self.telemetry.append(packet)
        return None

    def summary(self):
        problems = list(self.problems)
        if self.events != list(EVENT_TYPES):
            problems.append("session events were %s" % self.events)
        if self.heartbeats == 0:
            problems.append("no heartbeat arrived")
        if not self.telemetry:
            problems.append("no telemetry packet arrived")
        elif not self.telemetry[-1].get("end"):
            problems.append("the last telemetry packet is not marked end")
        return problems


class Handler(BaseHTTPRequestHandler):
    platform = None  # set by serve()

    def log_message(self, format, *args):  # keep the smoke output readable
        pass

    def reply(self, status, body):
        data = json.dumps(body).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def reject(self, text):
        self.platform.problem("%s %s: %s" % (self.command, self.path, text))
        self.reply(400, {"Error": "true", "Message": text})

    def body(self):
        length = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(length) if length else b""
        try:
            return json.loads(raw.decode("utf-8")) if raw else {}
        except ValueError:
            return None

    def authorized(self):
        header = self.headers.get("Authorization", "")
        return header.startswith("Bearer ") and header[7:] in self.platform.tokens

    def do_GET(self):
        path = urlsplit(self.path).path
        if path == "/ping":
            return self.reply(200, {})
        if path == "/v2/auth/validate-signature":
            if not self.headers.get("Authorization", "").startswith("Bearer "):
                return self.reject("no bearer token")
            return self.reply(200, fixture("token_login_response.json"))
        if path == "/v2/auth/quick-id/get-users":
            return self.reply(200, fixture("quick_id_users_response.json"))
        if re.fullmatch(r"/access/user/\d+/module/%d" % MODULE_ID, path):
            return self.reply(200, fixture("module_access_response.json"))
        return self.reject("unexpected request")

    def do_POST(self):
        path = urlsplit(self.path).path
        body = self.body()
        if body is None:
            return self.reject("body is not JSON")
        if path == "/login":
            if body.get("Login") != USERNAME or body.get("Password") != PASSWORD:
                return self.reject("wrong login body")
            return self.reply(200, fixture("login_response.json"))
        if path == "/v2/auth/quick-id/login":
            if not body.get("Username") or not body.get("SerialNumber"):
                return self.reject("Quick ID login needs Username and SerialNumber")
            return self.reply(200, fixture("quick_id_login_response.json"))
        if not self.authorized():
            return self.reject("missing or unknown bearer token")
        if path == "/v2/query":
            query = body.get("query", "")
            if "generateAuthCode" in query:
                return self.reply(200, fixture("generate_auth_code_response.json"))
            if "modules" in query:
                return self.reply(200, fixture("module_list_response.json"))
            return self.reject("unknown GraphQL query")
        if path == "/access/users":
            return self.reply(200, fixture("user_modules_response.json"))
        if path == "/event":
            problem = self.platform.check_event(body)
            if problem:
                return self.reject(problem)
            if body["eventType"] == EVENT_TYPES[0]:
                return self.reply(200, fixture("session_join_response.json"))
            return self.reply(200, {})
        if path == "/heartbeat/pulse":
            if body.get("sessionId") != self.platform.session_id:
                return self.reject("heartbeat sessionId is %r" % body.get("sessionId"))
            with self.platform.lock:
                self.platform.heartbeats += 1
            return self.reply(200, {})
        if path == "/telemetry":
            problem = self.platform.check_telemetry(body)
            if problem:
                return self.reject(problem)
            return self.reply(200, {})
        return self.reject("unexpected request")


def serve():
    platform = Platform()
    handler = type("BoundHandler", (Handler,), {"platform": platform})
    server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server, platform


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("mode", choices=("serve", "run"))
    parser.add_argument("--xapi", action="store_true", help="expect xAPI session statements (passes --xapi on)")
    parser.add_argument("command", nargs=argparse.REMAINDER, help="for run: -- COMMAND...")
    args = parser.parse_args()

    server, platform = serve()
    platform.xapi = args.xapi
    url = "http://127.0.0.1:%d" % server.server_address[1]
    options = ["--env", "custom", "--headset-url", url, "--platform-url", url, "--telemetry-url", url + "/telemetry",
               "--user", USERNAME, "--password", PASSWORD, "--module-id", str(MODULE_ID), "--quick-id-user", "ada",
               "--platform", "windows", "--data", DATA.decode("ascii"), "--timeout", "10"]
    if args.xapi:
        options.append("--xapi")

    if args.mode == "serve":
        print("Fake Apex platform at %s. Run the smoke test with:\n  apex_smoke %s" % (url, " ".join(options)))
        try:
            threading.Event().wait()
        except KeyboardInterrupt:
            return 0

    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("run needs a command after --")
    result = subprocess.run(command + options)
    server.shutdown()

    problems = platform.summary()
    for text in problems:
        print("fake_apex: " + text)
    if result.returncode != 0:
        print("fake_apex: the smoke test exited with %d" % result.returncode)
    if problems or result.returncode != 0:
        return 1
    print("fake_apex: every request matched the protocol")
    return 0


if __name__ == "__main__":
    sys.exit(main())
