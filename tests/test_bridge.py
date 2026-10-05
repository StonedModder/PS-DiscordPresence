import sys, json, threading, time, types
from http.server import BaseHTTPRequestHandler, HTTPServer
sys.path.insert(0, str(__import__("pathlib").Path(__file__).resolve().parent.parent))
import bridge

# activity / FW formatting
a = bridge.activity({"state": "active_running", "titleName": "Astro Bot", "firmware": "9.60"})
assert a["state"] == "Playing on PlayStation 5 FW 9.60", a
assert bridge.activity({"state": "active_running", "titleName": "X", "firmware": ""})["state"] == "Playing on PlayStation 5"
assert bridge.next_activity({"state": "probe_timeout"}) is bridge.KEEP
assert bridge.next_activity({"state": "loaded_unknown"}) is bridge.KEEP
assert bridge.activity({"state": "not_loaded"}) is None
# negative cache: bad content id fetch happens once
calls = []
real = bridge.urllib.request.urlopen
def boom(url, timeout=0):
    if "store.playstation.com" in url: calls.append(url); raise OSError("offline")
    return real(url, timeout=timeout)
bridge.urllib.request.urlopen = boom
bridge.playstation_metadata("UP9000-PPSA01234_00-ASTROBOT00000000"); bridge.playstation_metadata("UP9000-PPSA01234_00-ASTROBOT00000000")
assert len(calls) == 1, calls

# end-to-end loop against fake PS5 + fake Discord
seq = [{"state": "active_running", "titleName": "Game A", "firmware": "9.60"},
       {"state": "probe_timeout"},
       {"state": "loaded_unknown", "titleid": "CUSA01127"},
       {"state": "active_running", "titleName": "Game A", "firmware": "9.60"},
       {"state": "active_running", "titleName": 'Game "B"', "firmware": "9.60"},
       "DOWN",
       {"state": "not_loaded"}]
class H(BaseHTTPRequestHandler):
    def do_GET(self):
        cur = seq.pop(0) if seq else {"state": "not_loaded"}
        if cur == "DOWN": self.send_response(500); self.end_headers(); return
        body = json.dumps(cur).encode(); self.send_response(200); self.end_headers(); self.wfile.write(body)
    def log_message(self, *a): pass
srv = HTTPServer(("127.0.0.1", 0), H); threading.Thread(target=srv.serve_forever, daemon=True).start()
events = []
class P:
    def __init__(self, i): pass
    def connect(self): events.append("connect")
    def close(self): events.append("close")
    def clear(self): events.append("clear")
    def update(self, **k): events.append(("update", k["details"], k["start"]))
pp = types.ModuleType("pypresence"); pp.Presence = P
ex = types.ModuleType("pypresence.exceptions"); ex.DiscordNotFound = RuntimeError
sys.modules.update({"pypresence": pp, "pypresence.exceptions": ex})
bridge.load_config = lambda: bridge.Config("127.0.0.1", srv.server_port, "123", 3)
ticks = iter(range(100, 10000, 10)); bridge.time.time = lambda: next(ticks)
n = [0]
def fake_sleep(s):
    n[0] += 1
    if n[0] >= 8: raise KeyboardInterrupt
bridge.time.sleep = fake_sleep
try: bridge.main()
except KeyboardInterrupt: pass
print(events)
ups = [e for e in events if e[0] == "update"]
assert ups[0][1] == "Game A" and ups[1][1] == 'Game "B"' and ups[1][2] > ups[0][2], "timer resets per title"
assert len(ups) == 2, "probe_timeout must not clear/re-send"
assert events.count("clear") == 1, "PS5 down clears once, not_loaded stays cleared"
