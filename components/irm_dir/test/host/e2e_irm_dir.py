#!/usr/bin/env python3
"""Host-Test fuer irm_dir.c: lokaler HTTP-Server (127.0.0.1:18973) liefert die Beispielliste. Anfrage 1: langsam (in Stuecken, ~1,2 s),
Anfrage 2: HTTP 500, ab Anfrage 3: normal. Aufruf: e2e_irm_dir.py <kiwisdr_sample.js> <test_irm_dir_host>"""
import subprocess, sys, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
body = open(sys.argv[1], "rb").read()
n = [0]
class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *a): pass
    def do_GET(self):
        n[0] += 1; k = n[0]
        if k == 2:
            self.send_response(500); self.send_header("Content-Length", "0"); self.send_header("Connection", "close"); self.end_headers(); return
        self.send_response(200); self.send_header("Content-Length", str(len(body))); self.send_header("Connection", "close"); self.end_headers()
        if k == 1:
            half = len(body) // 2
            self.wfile.write(body[:half]); self.wfile.flush(); time.sleep(1.2); self.wfile.write(body[half:])
        else:
            self.wfile.write(body)
srv = ThreadingHTTPServer(("127.0.0.1", 18973), H)
threading.Thread(target=srv.serve_forever, daemon=True).start()
p = subprocess.run([sys.argv[2]], capture_output=True, text=True, timeout=120)
print(p.stdout); print(p.stderr) if p.stderr.strip() else None
print("HTTP-Anfragen beim Server:", n[0])
sys.exit(p.returncode if n[0] == 3 else 1)
