from __future__ import annotations
import hashlib, hmac, json, time, urllib.request, re
from dataclasses import dataclass, asdict, fields
from pathlib import Path

ART_CACHE: dict[str, dict[str, str]] = {}

APP_DIR = Path.home() / ".ps-discordpresence-bridge"
CONFIG = APP_DIR / "config.json"
# Public HMAC key for Sony's title metadata service (tmdb.np.dl.playstation.net).
TMDB_KEY = bytes.fromhex("F5DE66D2680E255B2DF79E74F890EBF349262F618BCAE2A9ACCDEE5156CE8DF2"
                         "CDF2D48C71173CDC2594465B87405D197CF1AED3B7E9671EEB56CA6753C2E6B0")

@dataclass
class Config:
    ps5_host: str = "192.168.69.8"
    ps5_port: int = 9878
    discord_application_id: str = ""
    poll_seconds: int = 5

def load_config() -> Config:
    APP_DIR.mkdir(parents=True, exist_ok=True)
    if not CONFIG.exists():
        c = Config(); CONFIG.write_text(json.dumps(asdict(c), indent=2)+"\n"); return c
    data = json.loads(CONFIG.read_text())
    return Config(**{f.name: data[f.name] for f in fields(Config) if f.name in data})

def fetch_status(c: Config) -> dict:
    with urllib.request.urlopen(f"http://{c.ps5_host}:{c.ps5_port}/api/status", timeout=8) as response:
        return json.load(response)

def playstation_metadata(content_id: str) -> dict[str, str]:
    if content_id in ART_CACHE:
        return ART_CACHE[content_id]
    if not re.fullmatch(r"[A-Z0-9_-]{8,80}", content_id or ""):
        return {}
    ART_CACHE[content_id] = {}  # negative cache: never refetch a failed lookup every poll
    try:
        page = urllib.request.urlopen(f"https://store.playstation.com/en-us/product/{content_id}", timeout=12).read().decode("utf-8", "replace")
        match = re.search(r'<script[^>]+application/ld\+json[^>]*>(.*?)</script>', page, re.I | re.S)
        data = json.loads(match.group(1)) if match else {}
        name, image = data.get("name", ""), data.get("image", "")
        if isinstance(name, str) and isinstance(image, str) and image.startswith("https://image.api.playstation.com/"):
            ART_CACHE[content_id] = {"name": name, "image": image}
    except Exception:
        pass
    return ART_CACHE[content_id]

def tmdb_metadata(title_id: str) -> dict[str, str]:
    """PS4 (CUSA) title name and icon by title ID; works for delisted games with no store page."""
    if title_id in ART_CACHE:
        return ART_CACHE[title_id]
    if not re.fullmatch(r"CUSA\d{5}", title_id or ""):
        return {}
    ART_CACHE[title_id] = {}
    np_title = f"{title_id}_00"
    digest = hmac.new(TMDB_KEY, np_title.encode(), hashlib.sha1).hexdigest().upper()
    try:
        with urllib.request.urlopen(f"https://tmdb.np.dl.playstation.net/tmdb2/{np_title}_{digest}/{np_title}.json", timeout=12) as response:
            data = json.load(response)
        name = (data.get("names") or [{}])[0].get("name", "")
        icon = next((i.get("icon", "") for i in data.get("icons") or [] if i.get("icon")), "")
        icon = re.sub(r"^http://", "https://", icon)
        if isinstance(name, str) and name:
            ART_CACHE[title_id] = {"name": name, "image": icon if icon.startswith("https://gs2-sec.ww.prod.dl.playstation.net/") else ""}
    except Exception:
        pass
    return ART_CACHE[title_id]

def activity(status: dict) -> dict | None:
    state = status.get("state")
    store = playstation_metadata(status.get("contentid", ""))
    if not store.get("image"):
        store = tmdb_metadata(status.get("titleid", "")) or store
    title = status.get("titleName") or store.get("name") or status.get("titleid") or "PlayStation 5"
    firmware = status.get("firmware")
    platform = f"Playing on PlayStation 5 FW {firmware}" if firmware else "Playing on PlayStation 5"
    if state not in {"active_running", "suspended_or_home", "guide_menu"}:
        return None
    result = {"details": title, "state": platform, "large_text": title}
    if store.get("image"):  # no art: let Discord show the application icon instead of a broken asset key
        result["large_image"] = store["image"]
    return result

OFFLINE_POLL_SECONDS = 15

def offline_reason(error: Exception) -> str:
    reason = getattr(error, "reason", error)  # URLError wraps the socket error
    if isinstance(reason, ConnectionRefusedError):
        return "PS5 refused the connection: the payload is not running (rest mode stops it; resend PS-DiscordPresence.elf after waking)"
    if isinstance(reason, OSError):
        return "PS5 is offline or in rest mode"
    return f"PS5 sent an unreadable status ({error})"

KEEP = object()  # status that should leave the current presence untouched

def next_activity(status: dict) -> dict | None | object:
    # Undecided states (probe too slow, or a title loaded with no recent focus event)
    # must not clear a presence that is still valid.
    if status.get("state") in {"probe_timeout", "loaded_unknown", "unknown"}:
        return KEEP
    return activity(status)

def main() -> None:
    c = load_config()
    if not c.discord_application_id.isdecimal():
        raise SystemExit(f"Set numeric discord_application_id in {CONFIG}")
    from pypresence import Presence
    from pypresence.exceptions import DiscordNotFound
    rpc = Presence(c.discord_application_id)
    try:
        rpc.connect()
    except DiscordNotFound:
        print("Bridge startup error: Discord desktop is not installed or running on this machine.")
        return
    last = None; title = None; started = 0; offline = False
    print(f"Bridge connected. Polling PS5 at {c.ps5_host}:{c.ps5_port}")
    while True:
        try:
            try:
                current = next_activity(fetch_status(c))
                if offline:
                    print("PS5 is back online")
                    offline = False
            except (OSError, ValueError) as error:
                if not offline:
                    print(f"{offline_reason(error)}. Waiting for it to come back...")
                    offline = True
                current = None
            if current is not KEEP:
                marker = json.dumps(current, sort_keys=True)
                if marker != last:
                    if current is None:
                        rpc.clear(); title = None; print("Discord presence cleared")
                    else:
                        if current["details"] != title:
                            title, started = current["details"], int(time.time())
                        rpc.update(**current, start=started); print("Discord presence updated:", current["details"])
                    last = marker
        except Exception as error:
            print("Discord connection lost, reconnecting:", error)
            last = None  # force a resend once Discord is back
            try:
                rpc.close()
            except Exception:
                pass
            try:
                rpc.connect()
            except Exception:
                pass
        time.sleep(OFFLINE_POLL_SECONDS if offline else max(3, c.poll_seconds))

if __name__ == "__main__": main()
