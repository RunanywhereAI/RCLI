#!/usr/bin/env python3
"""Hermetic CLI test for browser-approved cloud authentication."""

import json
import os
import pathlib
import stat
import subprocess
import sys
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


ACCESS_TOKEN = "account-e2e-access-secret"
REFRESH_TOKEN = "account-e2e-refresh-secret"
EMAIL = "developer@example.test"


class ConsoleHandler(BaseHTTPRequestHandler):
    requests = []
    console_origin = ""

    def log_message(self, _format, *_args):
        return

    def read_json(self):
        length = int(self.headers.get("Content-Length", "0"))
        return json.loads(self.rfile.read(length).decode("utf-8") or "{}")

    def reply(self, status, body=None):
        encoded = b"" if body is None else json.dumps(body).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def do_POST(self):
        body = self.read_json()
        self.requests.append(("POST", self.path, self.headers.get("Authorization"), body))
        if self.path == "/auth/cli/start":
            self.reply(
                200,
                {
                    "request_code": "ABCD-EFGH",
                    "poll_secret": "poll-secret",
                    "verification_url": self.console_origin + "/device?code=ABCD-EFGH",
                    "expires_in": 60,
                    "interval": 1,
                },
            )
        elif self.path == "/auth/cli/poll":
            self.reply(
                200,
                {
                    "status": "approved",
                    "access_token": ACCESS_TOKEN,
                    "refresh_token": REFRESH_TOKEN,
                    "email": EMAIL,
                    "expires_in": 3600,
                },
            )
        elif self.path == "/auth/cli/revoke":
            self.reply(204)
        else:
            self.reply(404, {"error": "unknown path"})

    def do_GET(self):
        self.requests.append(("GET", self.path, self.headers.get("Authorization"), None))
        if self.path == "/v1/me":
            self.reply(200, {"email": EMAIL})
        else:
            self.reply(404, {"error": "unknown path"})


def run(binary, arguments, environment):
    result = subprocess.run(
        [binary, *arguments],
        env=environment,
        capture_output=True,
        text=True,
        timeout=15,
        check=False,
    )
    combined = result.stdout + result.stderr
    if result.returncode != 0:
        raise AssertionError(
            f"{' '.join(arguments)} returned {result.returncode}:\n{combined}"
        )
    if ACCESS_TOKEN in combined or REFRESH_TOKEN in combined:
        raise AssertionError(f"{' '.join(arguments)} exposed a cloud token")
    return combined


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_account_cli.py /path/to/rcli")
    binary = sys.argv[1]
    server = ThreadingHTTPServer(("127.0.0.1", 0), ConsoleHandler)
    ConsoleHandler.console_origin = f"http://127.0.0.1:{server.server_port}"
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()

    try:
        with tempfile.TemporaryDirectory(prefix="rcli-account-e2e-") as profile:
            environment = os.environ.copy()
            environment["RCLI_PROFILE_DIR"] = profile
            environment["RCLI_CONSOLE_URL"] = ConsoleHandler.console_origin
            for name in (
                "RUNANYWHERE_API_KEY",
                "RUNANYWHERE_API_SECRET",
                "RUNANYWHERE_ENVIRONMENT",
            ):
                environment.pop(name, None)

            login = run(binary, ["login", "--no-browser"], environment)
            if "ABCD-EFGH" not in login or ConsoleHandler.console_origin not in login:
                raise AssertionError("login did not print the approval code and URL")

            files = list(pathlib.Path(profile).iterdir())
            if len(files) != 1:
                raise AssertionError("login did not create exactly one session file")
            if os.name != "nt":
                directory_mode = stat.S_IMODE(os.stat(profile).st_mode)
                file_mode = stat.S_IMODE(os.stat(files[0]).st_mode)
                if directory_mode != 0o700 or file_mode != 0o600:
                    raise AssertionError(
                        f"unsafe credential modes: {directory_mode:o}/{file_mode:o}"
                    )

            whoami = run(binary, ["whoami"], environment)
            if EMAIL not in whoami or "session" not in whoami or "active" not in whoami:
                raise AssertionError("whoami did not report the active identity")
            if "plan" in whoami or "tokens" in whoami or "quota" in whoami:
                raise AssertionError("whoami exposed launch-out-of-scope billing fields")

            run(binary, ["logout"], environment)
            if list(pathlib.Path(profile).iterdir()):
                raise AssertionError("logout did not remove the local session")

        expected = [
            ("POST", "/auth/cli/start", None),
            ("POST", "/auth/cli/poll", None),
            ("GET", "/v1/me", f"Bearer {ACCESS_TOKEN}"),
            ("POST", "/auth/cli/revoke", f"Bearer {ACCESS_TOKEN}"),
        ]
        actual = [(method, path, authorization) for method, path, authorization, _ in ConsoleHandler.requests]
        if actual != expected:
            raise AssertionError(f"unexpected console request sequence: {actual!r}")
        if ConsoleHandler.requests[1][3] != {
            "request_code": "ABCD-EFGH",
            "poll_secret": "poll-secret",
        }:
            raise AssertionError("poll request did not use the server-issued secret")
        if ConsoleHandler.requests[3][3] != {"refresh_token": REFRESH_TOKEN}:
            raise AssertionError("logout did not request refresh-token revocation")
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)

    print("account CLI browser flow passed")


if __name__ == "__main__":
    main()
