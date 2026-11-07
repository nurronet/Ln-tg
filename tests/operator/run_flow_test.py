"""Drives flow_harness.exe against a fake ERP and checks what went over the wire and onto disk.

Usage (any shell with gcc + the station's libraries; see README.md):
    python run_flow_test.py <path to flow_harness.exe>
"""
import glob
import json
import os
import subprocess
import sys
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer

seen = []  # (path, headers, body text), in arrival order
logins = [0]


class FakeErp(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", 0))).decode()
        method = self.path.rsplit(".", 1)[-1]
        seen.append((self.path, dict(self.headers), body))
        if self.headers.get("Authorization") != "token KEY:SECRET":
            raw = b"<html>Forbidden</html>"
            self.send_response(403)
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)
            return
        data = json.loads(body) if body else {}
        if method == "operator_login":
            if data.get("pin") == "483920" and data.get("operator_id") == "OP1" and data.get("station_id") == "LN-TEST-01":
                logins[0] += 1
                out = {"message": {"ok": True, "operator_id": "OP1", "operator_name": "Ana Ruiz",
                                   "session": "TOKEN-A" if logins[0] == 1 else "TOKEN-B", "expires_at": "x"}}
            else:
                out = {"message": {"ok": False, "reason": "The Operator ID or PIN is wrong."}}
        elif method == "operator_logout":
            out = {"message": {"ok": True}}
        elif method == "submit_timing_result":
            token = data.get("operator_session")
            if token == "TOKEN-A":
                out = {"message": {"verification_status": "Attested", "session": "MS-1"}}
            else:
                why = "The operator sign-in has expired. Sign in again." if token else "No operator sign-in was sent."
                out = {"message": {"verification_status": "Invalid", "verification_message": "Operator sign-in: " + why,
                                   "session": "MS-1"}}
        elif method == "register_key":
            out = {"message": {"ok": True}}
        else:
            self.send_response(404)
            self.end_headers()
            return
        raw = json.dumps(out).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


def main():
    exe = sys.argv[1]
    server = HTTPServer(("127.0.0.1", 0), FakeErp)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    export_dir = tempfile.mkdtemp()
    config_dir = tempfile.mkdtemp()  # the station key is generated here, not in the real user config
    env = dict(os.environ, XDG_CONFIG_HOME=config_dir)
    out = subprocess.run([exe, "http://127.0.0.1:%d" % server.server_port, export_dir],
                         capture_output=True, text=True, env=env, timeout=120)
    server.shutdown()
    print(out.stdout)
    assert out.returncode == 0, out.stderr
    lines = {line.split(" ", 1)[0]: line for line in out.stdout.splitlines()}
    fails = []

    def check(cond, what):
        if not cond:
            fails.append(what)

    check("rc=-2" in lines["short_pin"] and "signed_in=0" in lines["short_pin"], "short PIN refused locally")
    check("rc=1" in lines["wrong_pin"] and "PIN is wrong" in lines["wrong_pin"] and "signed_in=0" in lines["wrong_pin"],
          "wrong PIN refused with the ERP's own words")
    check("rc=0" in lines["good_pin"] and "operator_id=OP1" in lines["good_pin"] and "name=Ana Ruiz" in lines["good_pin"]
          and "signed_in=1" in lines["good_pin"] and "usable=1" in lines["good_pin"], "good PIN signs in")
    check("rc=0 noted=0 signed_in=1" in lines["submit_a"], "an attested submit keeps the sign-in")
    check("touched=1" in lines["submit_a"], "a result counts as use of the sign-in")
    check("usable=0 signed_in=1" in lines["stale"], "a stale sign-in is not usable")
    check("signed_in=0" in lines["signed_out"], "sign out forgets the token")
    check("noted=1 signed_in=0" in lines["submit_b"], "a rejected sign-in is dropped")
    check("noted=0" in lines["submit_c"], "nothing to drop when signed out")
    check("rc=-1" in lines["unreachable"] and "msg=Could not reach the ERP" in lines["unreachable"]
          and "signed_in=0" in lines["unreachable"], "an unreachable ERP is not a sign-in and says so")
    check("noted=0 signed_in=1" in lines["non2xx"], "a failed request does not end the sign-in")
    check("rc=-1" in lines["forbidden"] and "API key" in lines["forbidden"] and "<html>" not in lines["forbidden"]
          and "signed_in=0" in lines["forbidden"], "a refused API key is explained in words")

    logins_seen = [s for s in seen if s[0].endswith("operator_login")]
    check(len(logins_seen) == 5, "the short PIN never went over the wire (saw %d logins, wanted 5)" % len(logins_seen))
    check(sum(1 for s in seen if s[1].get("Authorization") != "token KEY:SECRET") == 1, "API credentials sent (one deliberate bad key)")
    if len(logins_seen) >= 2:
        check(json.loads(logins_seen[1][2])["operator_id"] == "OP1", "operator ID trimmed")
    submits = [s for s in seen if s[0].endswith("submit_timing_result")]
    check(len(submits) == 4, "four submits")
    if len(submits) == 4:
        check(json.loads(submits[0][2]).get("operator_session") == "TOKEN-A", "token in the first result")
        check(json.loads(submits[1][2]).get("operator_session") == "TOKEN-B", "token in the second result")
        check("operator_session" not in json.loads(submits[2][2]), "no token once it was dropped")
        check("operator_session" not in json.loads(submits[3][2]), "no stale token sent")
    logouts = [s for s in seen if s[0].endswith("operator_logout")]
    check(len(logouts) == 1 and json.loads(logouts[0][2]) == {"session": "TOKEN-A"}, "logout sends the token")

    # nothing secret on disk: the export and the submit log
    disk = ""
    for name in glob.glob(os.path.join(export_dir, "*")):
        disk += open(name, encoding="utf-8", errors="replace").read()
    check(disk, "something was written to the export directory")
    check(any(n.endswith(".json") for n in os.listdir(export_dir)), "an export file exists")
    check(any(n.endswith(".log") for n in os.listdir(export_dir)), "a submit log exists")
    for secret in ("483920", "483921", "12345", "TOKEN-A", "TOKEN-B", "operator_session"):
        check(secret not in disk, "%s must not reach disk" % secret)

    print("FAIL: " + "; ".join(fails) if fails else "ALL OK")
    sys.exit(1 if fails else 0)


main()
