import sys, json, threading, types, socket, pathlib
from http.server import BaseHTTPRequestHandler, HTTPServer
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import bridge

# real refused connection: bind then close a port so nothing listens on it
s = socket.socket(); s.bind(("127.0.0.1", 0)); dead = s.getsockname()[1]; s.close()
try: bridge.fetch_status(bridge.Config("127.0.0.1", dead))
except OSError as e: assert "payload is not running" in bridge.offline_reason(e), bridge.offline_reason(e)

# up -> down for 3 polls -> up again: one offline line, one clear, one online line, presence restored
state = {"up": True}
class H(BaseHTTPRequestHandler):
    def do_GET(self):
        body = json.dumps({"state": "active_running", "titleName": "Game A"}).encode()
        self.send_response(200); self.end_headers(); self.wfile.write(body)
    def log_message(self, *a): pass
srv = HTTPServer(("127.0.0.1", 0), H); threading.Thread(target=srv.serve_forever, daemon=True).start()
plan = [True, False, False, False, True]
real_fetch = bridge.fetch_status
def fetch(c):
    if not plan.pop(0): return real_fetch(bridge.Config("127.0.0.1", dead))
    return real_fetch(c)
bridge.fetch_status = fetch
events, sleeps = [], []
class P:
    def __init__(self, i): pass
    def connect(self): pass
    def close(self): pass
    def clear(self): events.append("clear")
    def update(self, **k): events.append("update")
pp = types.ModuleType("pypresence"); pp.Presence = P
ex = types.ModuleType("pypresence.exceptions"); ex.DiscordNotFound = RuntimeError
sys.modules.update({"pypresence": pp, "pypresence.exceptions": ex})
bridge.load_config = lambda: bridge.Config("127.0.0.1", srv.server_port, "123", 5)
def fake_sleep(t):
    sleeps.append(t)
    if not plan: raise KeyboardInterrupt
bridge.time.sleep = fake_sleep
try: bridge.main()
except KeyboardInterrupt: pass
assert events == ["update", "clear", "update"], events
assert sleeps == [5, 15, 15, 15, 5], sleeps
print("offline test ok")
