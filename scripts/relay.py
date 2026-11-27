import os
import re
import secrets
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(os.environ.get("PORT", "8080"))

CODE = re.compile(r"^[A-HJKMNP-Z2-9]{6}$")
KEY = re.compile(r"^[A-HJKMNP-Z2-9]{16}$")

MAX_ROOMS = 500
MAX_REQUESTS = 200
MAX_BODY = 4096
MAX_LINES = 32
LINE_LIMIT = 256
QUEUE_LIMIT = 500
IDLE_TIMEOUT = 60
GAME_MAX_SECONDS = 6 * 3600
GAME_MAX_BYTES = 1_000_000
POLL_MAX = 20

lock = threading.Condition()
rooms = {}
requests = 0


def now():
    return time.monotonic()


def role_of(room, key):
    if key == room["host_key"]:
        return "host"
    if room["guest_key"] and key == room["guest_key"]:
        return "guest"
    return None


def other(role):
    return "guest" if role == "host" else "host"


def new_key():
    return "".join(secrets.choice("ABCDEFGHJKMNPQRSTUVWXYZ23456789") for _ in range(16))


def new_room(owner):
    return {"owner": owner, "host_key": new_key(), "guest_key": None, "queue": {"host": [], "guest": []}, "bytes": {"host": 0, "guest": 0},
            "seen": now(), "born": now()}


def cleanup():
    while True:
        time.sleep(10)
        with lock:
            for code in [c for c, r in rooms.items() if now() - r["seen"] > IDLE_TIMEOUT or now() - r["born"] > GAME_MAX_SECONDS]:
                del rooms[code]
            lock.notify_all()


def do_host(code, key):
    if not CODE.match(code) or not KEY.match(key):
        return 400, ""

    with lock:
        room = rooms.get(code)

        if room and room["owner"] != key:
            return 409, "TAKEN"

        if room is None and len(rooms) >= MAX_ROOMS:
            return 503, "FULL"

        rooms[code] = new_room(key)
        lock.notify_all()

        return 200, "OK " + rooms[code]["host_key"]


def do_join(code):
    if not CODE.match(code):
        return 400, ""

    with lock:
        room = rooms.get(code)

        if room and room["guest_key"] is None:
            room["guest_key"] = new_key()
            room["queue"]["host"].append("PAIRED")
            room["seen"] = now()
            lock.notify_all()
            return 200, "OK " + room["guest_key"]

        if room:
            return 409, "TAKEN"

    time.sleep(1) # makes guessing codes slow
    return 404, "NOTFOUND"


def do_poll(code, key, wait):
    deadline = now() + max(0, min(wait, POLL_MAX))

    with lock:
        while True:
            room = rooms.get(code)
            role = role_of(room, key) if room else None

            if role is None:
                return 404, ""

            room["seen"] = now()
            queue = room["queue"][role]

            if queue:
                lines, room["queue"][role] = queue[:], []
                return 200, "\n".join(lines)

            remaining = deadline - now()

            if remaining <= 0:
                return 204, ""

            lock.wait(remaining)


def do_send(code, key, lines):
    with lock:
        room = rooms.get(code)
        role = role_of(room, key) if room else None

        if role is None:
            return 404, ""

        room["seen"] = now()

        if role == "host" and room["guest_key"] is None:
            return 409, ""

        target = room["queue"][other(role)]

        for line in lines[:MAX_LINES]:
            room["bytes"][role] += len(line) + 1

            if len(line) > LINE_LIMIT or room["bytes"][role] > GAME_MAX_BYTES or len(target) >= QUEUE_LIMIT:
                del rooms[code]
                lock.notify_all()
                return 404, ""

            target.append(line)

        lock.notify_all()

    return 200, ""


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "relay"

    def reply(self, status, body=""):
        data = body.encode("ascii", "replace")
        self.send_response(status)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        self.reply(200, "chess relay")

    def do_POST(self):
        global requests

        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            length = -1

        if length < 0 or length > MAX_BODY:
            self.close_connection = True
            return self.reply(413)

        text = self.rfile.read(length).decode("ascii", "replace")
        first, _, rest = text.partition("\n")
        parts = first.strip().split(" ")

        with lock:
            if requests >= MAX_REQUESTS:
                return self.reply(503)

            requests += 1

        try:
            if self.path == "/host" and len(parts) == 2:
                status, body = do_host(parts[0], parts[1])
            elif self.path == "/join" and len(parts) == 1:
                status, body = do_join(parts[0])
            elif self.path == "/poll" and len(parts) == 3 and parts[2].isdigit():
                status, body = do_poll(parts[0], parts[1], int(parts[2]))
            elif self.path == "/send" and len(parts) == 2:
                status, body = do_send(parts[0], parts[1], [l.rstrip("\r") for l in rest.split("\n") if l])
            else:
                status, body = 400, ""
        finally:
            with lock:
                requests -= 1

        self.reply(status, body)

    def log_message(self, *args):
        pass # nothing about players is logged


if __name__ == "__main__":
    ThreadingHTTPServer.daemon_threads = True
    ThreadingHTTPServer.request_queue_size = 64
    threading.Thread(target=cleanup, daemon=True).start()
    print(f"Relay listening on port {PORT}", flush=True)
    ThreadingHTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
