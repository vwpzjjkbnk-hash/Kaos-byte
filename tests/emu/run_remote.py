#!/usr/bin/env python3
"""The phone remote end to end: the built KaosByte.xex (Peer Pressure, drawn menu) on an emulated
PowerPC, serving a real browser.

Builds on run_dll.py's fake console (Unicorn PowerPC 970, fake loader, kernel, XAM, filesystem,
Skylanders: Trap Team). XAM's socket calls (NetDll_*) are backed by real sockets on this machine,
so the plugin's web server, running as emulated Xbox 360 code, listens on a local port:

  - the full release folder (every figure, the menu pictures, UI\\remote.html) is the data folder;
  - the plugin starts, puts nothing on the rift, and opens the phone remote after its delay;
  - plain HTTP checks: the page, the API (state, every list page, actions), refused paths;
  - a headless phone-sized Chromium (Playwright) loads the page from the emulated Xbox, browses,
    opens a figure and puts it on the rift; screenshots go next to the .xex;
  - the fake game then polls the portal and must see that figure arrive.

Given the Dashlaunch build (KaosByteDSL.xex, RGH / JTAG / BadUpdate) it boots on the dashboard
first, starts the game, has the server start on the game's input thread, runs the same checks,
then has the game exit (the remote stops with it) and start again (the remote comes back).

Usage: run_remote.py <KaosByte.xex | KaosByteDSL.xex> <.elf> <The Static Rift folder> [screenshot dir]
(needs Unicorn's Python binding as run_dll.py does, and node with playwright for the browser part)
"""

import json
import os
import select
import socket
import struct
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run_dll as rd  # noqa: E402
from run_dll import STACKS, XAM_GETRAW, XAM_SETRAW, LIB_XAM, check  # noqa: E402

HOST_PORT = int(os.environ.get("REMOTE_TEST_PORT", "18080"))
ROOT = "hdd/The Static Rift/"
XBOX_IP = 0xC0A80117  # 192.168.1.23

# ------------------------------------------------------------------------------------------------
# A faster fake filesystem: a lower-case index, and pictures read from disk when first asked for.
# ------------------------------------------------------------------------------------------------
class DiskBackedFs(rd.FakeFs):
    def __init__(self, disk_root):
        super().__init__()
        self.disk_root = disk_root
        self.index = {"hdd": "hdd"}

    def key(self, path):
        k = self.index.get(path.lower())
        if k is not None and k in self.nodes:
            return k
        for k in self.nodes:  # added straight into nodes by the fake NtCreateFile
            if k.lower() == path.lower():
                self.index[path.lower()] = k
                return k
        for folder in ("images/", "phone/"):  # pictures: loaded when first asked for
            prefix = ROOT.lower() + folder
            if path.lower().startswith(prefix):
                disk = os.path.join(self.disk_root, path[len(ROOT):].split("/")[0],
                                    *path[len(prefix):].split("/"))
                if os.path.isfile(disk):
                    self.write(path, open(disk, "rb").read())
                    return path
        return None

    def mkdirs(self, path):
        parts = path.split("/")
        for i in range(1, len(parts) + 1):
            p = "/".join(parts[:i])
            if p.lower() not in self.index:
                self.nodes[p] = None
                self.index[p.lower()] = p

    def write(self, path, data):
        self.mkdirs(path.rsplit("/", 1)[0])
        self.nodes[path] = bytearray(data)
        self.index[path.lower()] = path


# ------------------------------------------------------------------------------------------------
# XAM sockets on real sockets
# ------------------------------------------------------------------------------------------------
class Net:
    """XAM's sockets on real ones. With XBOX_LIMITS=1 it also behaves like the console's stack where
    that matters for a web server: a fixed pool of TCP sockets (XNet's default is 32), a socket the
    plugin closes first staying in TIME_WAIT for a while (holding its slot), one shared Wi-Fi link
    (XBOX_RATE bytes/s, default 1.5 MB/s), and Nagle + delayed ACK costing 200 ms when a response
    goes out as a small send followed by another (unless TCP_NODELAY is set)."""

    def __init__(self):
        self.socks = {}
        self.next = 0x5000
        self.insecure = set()
        self.nodelay = set()
        self.nonblocking = set()
        self.peer_closed = set()
        self.small_pending = set()
        self.bound = {}
        self.error = 0
        self.limits = os.environ.get("XBOX_LIMITS") == "1"
        self.pool = int(os.environ.get("XBOX_POOL", "32"))
        self.time_wait = float(os.environ.get("XBOX_TIMEWAIT", "60"))
        self.rate = float(os.environ.get("XBOX_RATE", str(1.5 * 1024 * 1024)))
        self.drop = float(os.environ.get("XBOX_DROP", "0"))  # chance a send kills the connection
        self.rng = __import__("random").Random(7)
        self.waiting = []  # TIME_WAIT expiry times
        self.accepted = set()  # connections from phones (for a Wi-Fi blip)
        self.blip_until = 0.0
        self.stats = {"accepted": 0, "refused": 0, "bytes": 0, "sends": 0, "nagle": 0, "max_used": 0,
                      "time_wait_peak": 0}

    def add(self, s):
        h = self.next
        self.next += 4
        self.socks[h] = s
        return h

    def used(self):
        now = time.time()
        self.waiting = [t for t in self.waiting if t > now]
        self.stats["time_wait_peak"] = max(self.stats["time_wait_peak"], len(self.waiting))
        return len(self.socks) + len(self.waiting)

    def check_blip(self):
        """A Wi-Fi blip, asked for by the browser test writing seconds into $XBOX_BLIP_FILE: every
        phone connection is reset, and new ones are reset too until the blip is over."""
        path = os.environ.get("XBOX_BLIP_FILE")
        if not path or not os.path.exists(path):
            return
        try:
            secs = float(open(path).read().strip() or "3")
            os.remove(path)
        except (OSError, ValueError):
            secs = 3.0
        self.blip_until = time.time() + secs
        self.stats["blips"] = self.stats.get("blips", 0) + 1
        for h in list(self.accepted):
            reset(self.socks.get(h))

    def room(self):
        n = self.used()
        self.stats["max_used"] = max(self.stats["max_used"], n)
        return not self.limits or n < self.pool


WSAEWOULDBLOCK, WSAENOBUFS = 10035, 10055


def reset(s):
    """The connection is gone, as when the Wi-Fi drops (the peer sees a reset)."""
    if s is None:
        return
    try:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
        s.shutdown(socket.SHUT_RDWR)
    except OSError:
        pass


def install_net(c):
    net = c.net = Net()

    def n_startup(c, caller, params):
        check(caller == 1, "XNetStartup as the title")
        return 0

    def n_wsa(c, caller, version, data):
        check(caller == 1 and version == 0x0202, "WSAStartup 2.2 as the title")
        return 0

    def n_socket(c, caller, af, typ, proto):
        check((af, typ, proto) == (2, 1, 6), "a TCP socket")
        if not net.room():
            net.error = WSAENOBUFS
            return 0xFFFFFFFF
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        return net.add(s)

    def n_setsockopt(c, caller, h, level, name, val, length):
        s = net.socks.get(h)
        if s is None:
            return 0xFFFFFFFF
        value = c.u32(val) if length >= 4 else 0
        if level == 0xFFFF and name == 0x5801:
            net.insecure.add(h)
        elif level == 0xFFFF and name in (0x1005, 0x1006):
            if h not in net.nonblocking:
                s.settimeout(max(value, 1) / 1000.0)
        elif level == 6 and name == 1:
            (net.nodelay.add if value else net.nodelay.discard)(h)
        return 0

    def n_ioctl(c, caller, h, cmd, argp):
        s = net.socks.get(h)
        if s is None:
            return 0xFFFFFFFF
        if cmd == 0x8004667E:  # FIONBIO
            on = c.u32(argp) != 0
            s.setblocking(not on)
            (net.nonblocking.add if on else net.nonblocking.discard)(h)
            return 0
        return 0xFFFFFFFF

    def n_bind(c, caller, h, addr, length):
        family, port, ip = struct.unpack(">HHI", c.mem(addr, 8))
        check(family == 2 and ip == 0, "bind to every address")
        net.bound[h] = port
        host_port = HOST_PORT if port == 80 else HOST_PORT + 1
        try:
            net.socks[h].bind(("127.0.0.1", host_port))
        except OSError:
            return 0xFFFFFFFF
        return 0

    def n_listen(c, caller, h, backlog):
        check(h in net.insecure, "the listening socket is marked insecure before it listens")
        net.socks[h].listen(int(backlog))
        return 0

    def read_set(addr):
        if not addr:
            return []
        return [c.u32(addr + 4 + 4 * i) for i in range(c.u32(addr))]

    def write_set(addr, handles):
        if addr:
            c.put32(addr, len(handles))
            for i, h in enumerate(handles):
                c.put32(addr + 4 + 4 * i, h)

    def n_select(c, caller, nfds, readfds, writefds, exceptfds, timeout):
        net.check_blip()
        rh_, wh_ = read_set(readfds), read_set(writefds)
        rs = [net.socks[h] for h in rh_ if h in net.socks]
        ws = [net.socks[h] for h in wh_ if h in net.socks]
        wait = 0.02
        if timeout:
            sec, usec = c.u32(timeout), c.u32(timeout + 4)
            wait = min(wait, sec + usec / 1e6)
        r, w, _ = select.select(rs, ws, [], wait) if (rs or ws) else ([], [], [])
        if not rs and not ws:
            time.sleep(wait)
        ro = [h for h in rh_ if net.socks.get(h) in r]
        wo = [h for h in wh_ if net.socks.get(h) in w]
        write_set(readfds, ro)
        write_set(writefds, wo)
        if exceptfds:
            c.put32(exceptfds, 0)
        c.sleeps += 1  # an idle wait counts as a sleep, so the emulator can switch threads
        if c.yield_after is not None and c.sleeps >= c.yield_after:
            c.stop_now = True
        return len(ro) + len(wo)

    def n_accept(c, caller, h, addr, addrlen):
        try:
            s, peer = net.socks[h].accept()
        except (BlockingIOError, InterruptedError):
            net.error = WSAEWOULDBLOCK
            return 0xFFFFFFFF
        except OSError:
            return 0xFFFFFFFF
        if time.time() < net.blip_until:  # the Wi-Fi is down: it never arrives
            net.stats["blip_refused"] = net.stats.get("blip_refused", 0) + 1
            s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
            s.close()
            net.error = WSAEWOULDBLOCK
            return 0xFFFFFFFF
        if not net.room():  # no socket for it: the console refuses the connection
            net.stats["refused"] += 1
            s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
            s.close()
            net.error = WSAENOBUFS
            return 0xFFFFFFFF
        net.stats["accepted"] += 1
        s.setblocking(True)
        s.settimeout(4.0)
        nh = net.add(s)
        net.accepted.add(nh)
        if addr:
            c.mu.mem_write(addr, struct.pack(">HHI", 2, peer[1], 0x7F000001) + bytes(8))
        return nh

    def n_recv(c, caller, h, buf, length, flags):
        s = net.socks.get(h)
        if s is None:
            return 0xFFFFFFFF
        try:
            data = s.recv(int(length))
        except (BlockingIOError, InterruptedError):
            net.error = WSAEWOULDBLOCK
            return 0xFFFFFFFF
        except OSError:
            return 0xFFFFFFFF
        if not data:
            net.peer_closed.add(h)
        net.small_pending.discard(h)  # the peer talked: its ACKs went with it
        c.mu.mem_write(buf, data)
        return len(data)

    def n_send(c, caller, h, buf, length, flags):
        s = net.socks.get(h)
        if s is None:
            return 0xFFFFFFFF
        data = bytes(c.mem(buf, length))
        if net.drop and net.rng.random() < net.drop:  # Wi-Fi trouble: the connection is gone
            net.stats["dropped"] = net.stats.get("dropped", 0) + 1
            reset(s)
            net.error = 10054  # WSAECONNRESET
            return 0xFFFFFFFF
        if net.limits:
            if h not in net.nodelay and h in net.small_pending:
                net.stats["nagle"] += 1
                time.sleep(0.2)  # Nagle holds this until the peer's (delayed) ACK
            if length < 1460 and h not in net.nodelay:
                net.small_pending.add(h)
            time.sleep(length / net.rate)  # the Wi-Fi link
        try:
            if h in net.nonblocking:
                n = s.send(data)
            else:
                s.sendall(data)
                n = length
        except (BlockingIOError, InterruptedError):
            net.error = WSAEWOULDBLOCK
            return 0xFFFFFFFF
        except OSError:
            return 0xFFFFFFFF
        net.stats["bytes"] += n
        net.stats["sends"] += 1
        return n

    def n_shutdown(c, caller, h, how):
        try:
            net.socks[h].shutdown(socket.SHUT_WR)
        except (OSError, KeyError):
            pass
        return 0

    def n_close(c, caller, h):
        s = net.socks.pop(h, None)
        if s is None:
            return 0xFFFFFFFF
        if net.limits and h not in net.peer_closed and h not in net.bound:
            net.waiting.append(time.time() + net.time_wait)  # we closed first: TIME_WAIT
        for st in (net.nodelay, net.nonblocking, net.peer_closed, net.small_pending, net.insecure, net.accepted):
            st.discard(h)
        s.close()
        return 0

    def n_xnaddr(c, caller, addr):
        c.mu.mem_write(addr, struct.pack(">IIH", XBOX_IP, 0, 0) + bytes(26))
        return 0x2 | 0x8  # Ethernet, DHCP

    def n_error(c):
        e, net.error = net.error, 0
        return e

    table = {
        0x003: ("NetDll_socket", 4, n_socket), 0x004: ("NetDll_closesocket", 2, n_close),
        0x005: ("NetDll_shutdown", 3, n_shutdown), 0x006: ("NetDll_ioctlsocket", 4, n_ioctl),
        0x007: ("NetDll_setsockopt", 6, n_setsockopt),
        0x00B: ("NetDll_bind", 4, n_bind), 0x00D: ("NetDll_listen", 3, n_listen),
        0x00E: ("NetDll_accept", 4, n_accept), 0x00F: ("NetDll_select", 6, n_select),
        0x012: ("NetDll_recv", 5, n_recv), 0x016: ("NetDll_send", 5, n_send),
        0x01B: ("NetDll_WSAGetLastError", 0, n_error), 0x001: ("NetDll_WSAStartup", 3, n_wsa),
        0x033: ("NetDll_XNetStartup", 2, n_startup), 0x049: ("NetDll_XNetGetTitleXnAddr", 2, n_xnaddr),
    }
    for ordinal, entry in table.items():
        rd.EXPORTS[(LIB_XAM, ordinal)] = entry


# ------------------------------------------------------------------------------------------------
def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    xex_path, elf_path, folder = sys.argv[1], sys.argv[2], sys.argv[3]
    shots = sys.argv[4] if len(sys.argv) > 4 else os.path.dirname(os.path.abspath(xex_path))
    xex = rd.parse_xex(open(xex_path, "rb").read())
    sym = rd.elf_symbols(elf_path)
    c = rd.Console(xex, sym)
    c.fs = DiskBackedFs(folder)
    install_net(c)

    # The data folder: everything but the pictures (those load when asked for).
    loaded = 0
    for dirpath, dirs, files in os.walk(folder):
        rel = os.path.relpath(dirpath, folder)
        if rel.split(os.sep)[0] in ("Images", "Phone"):
            continue
        for name in files:
            parts = [] if rel == "." else rel.split(os.sep)
            c.fs.write(ROOT + "/".join(parts + [name]), open(os.path.join(dirpath, name), "rb").read())
            loaded += 1
    c.fs.mkdirs(ROOT + "Images")
    page = os.environ.get("REMOTE_PAGE") or os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "..", "dist", "The Static Rift", "UI", "remote.html")
    c.fs.write(ROOT + "UI/remote.html", open(page, "rb").read())
    # The test's own settings: the shipped ones, minus remembered figures.
    ini = bytes(c.fs.read(ROOT + "staticrift.ini") or b"").decode("latin-1")
    c.fs.write(ROOT + "staticrift.ini", ini.encode("latin-1"))
    c.fs.nodes.pop(ROOT + "rift_state.txt", None)
    print(f"data folder: {loaded} files (pictures load on demand)")

    game_stack = STACKS + 0x10000
    getraw_var, getraw_thunk = c.game_imports[XAM_GETRAW]
    setraw_var, setraw_thunk = c.game_imports[XAM_SETRAW]

    def log():
        return bytes(c.fs.read(ROOT + "staticrift.log") or b"").decode("latin-1")

    start = next((v for k, v in sym.items() if "ServerThread" in k and "remote" in k), None)
    rgh = xex["flags"] == 0x8  # the Dashlaunch build (a system DLL)

    def server_from_game():
        """The game's input thread polls the controller (through the hook): the server the plugin
        asked for starts there, as one of the game's threads."""
        before = len(c.threads)
        _, thunk = c.game_imports[rd.XAM_GETSTATE]
        c.call(thunk, [0, 0, STACKS + 0x8300], game_stack, label="game XamInputGetState")
        t = next((t for t in c.threads[before:] if t["start"] == start), None)
        check(t is not None and not t["flags"] & 2,
              "the remote's server starts as one of the game's threads (not a system thread)")
        return t

    if rgh:
        print("boot: the dashboard runs; Dashlaunch loads the plugin (RGH / JTAG / BadUpdate build)")
        c.title_id = 0xFFFE07D1
        c.put32(rd.XEXEC_HANDLE_VAR, 0)
        c.system_dll = True
        check(c.call(xex["entry"], [0xD11, 1, 0], game_stack, label="DllMain attach") == 1, "DllMain returns TRUE")
        worker = c.threads[0]
        check(worker["flags"] & 2, "the worker is a system thread")
        c.run_thread(worker, 5)
        check(not any(t["start"] == start for t in c.threads), "no phone remote on the dashboard")
        print("Trap Team starts")
        c.title_id = 0x41560911
        c.put32(rd.XEXEC_HANDLE_VAR, rd.GAME_LDR)
        t0 = time.time()
        c.run_thread(worker, 3)
        print(f"   game start (scan of every figure) took {time.time() - t0:.1f} s")
        check("Phone remote: starting with the game" in log(), "the remote asks to start with the game")
        server = server_from_game()
    else:
        print("DllMain(DLL_PROCESS_ATTACH) in Trap Team")
        check(c.call(xex["entry"], [0xD11, 1, 0], game_stack, label="DllMain attach") == 1, "DllMain returns TRUE")
        worker = c.threads[0]
        t0 = time.time()
        c.run_thread(worker, 3)
        print(f"   startup (scan of every figure) took {time.time() - t0:.1f} s")
        server = next((t for t in c.threads if t["start"] == start), None)
        check(server is not None, f"the remote's thread started ({len(c.threads)} threads, ServerThread at {start})")

    for line in log().splitlines():
        print("   log|", line)
    if server is None:
        return finish()

    # Run both threads until the server says it's listening (8 s of the game's time first).
    def pump(rounds=1):
        for _ in range(rounds):
            if server is not None:  # (none while no game is running: RGH, on the dashboard)
                c.run_thread(server, 4)
            c.run_thread(worker, 2)

    for _ in range(200):
        pump()
        if "Phone remote: listening" in log():
            break
    check("Phone remote: listening - open http://192.168.1.23 on a phone on the same network" in log(),
          "log: the remote is listening, with the Xbox's address")
    pump(3)
    check(any(t == "Phone remote: open http://192.168.1.23" for t in c.toasts), "toast: the phone address")

    if os.environ.get("REMOTE_MODE") == "load":
        return run_load(c, pump, shots, log)

    # HTTP from this machine, on a thread, while the emulator keeps running.
    results = {}

    def client(name, method, path):
        def go():
            req = urllib.request.Request(f"http://127.0.0.1:{HOST_PORT}{path}", method=method)
            try:
                with urllib.request.urlopen(req, timeout=60) as r:
                    results[name] = (r.status, r.headers.get("Content-Type"), r.read())
            except urllib.error.HTTPError as e:
                results[name] = (e.code, e.headers.get("Content-Type"), e.read())
            except Exception as e:  # noqa: BLE001
                results[name] = (0, "", str(e).encode())
        t = threading.Thread(target=go)
        t.start()
        deadline = time.time() + 90
        while t.is_alive() and time.time() < deadline:
            pump()
        t.join(1)
        return results.get(name, (0, "", b"timeout"))

    print("HTTP: the page, the API, refused paths")
    st, ct, body = client("page", "GET", "/")
    check(st == 200 and ct.startswith("text/html") and b"THE STATIC RIFT" in body, f"/ is the page ({st}, {len(body)} bytes)")
    st, ct, body = client("state", "GET", "/api/state")
    state = json.loads(body) if st == 200 else {}
    check(st == 200 and state.get("gameName") == "Trap Team" and state.get("rift") == [], f"state: Trap Team, empty rift ({body[:200]})")
    views = state.get("views", [])
    print("   views:", views)
    check(views[:1] == [[0, "All"]] and [1, "My Picks"] in views and [5, "Trap Team"] in views,
          "views: All, My Picks and the games Trap Team can use")
    # Every page of All.
    items, frm, pages = [], 0, 0
    t0 = time.time()
    while frm >= 0 and pages < 40:
        st, ct, body = client(f"list{pages}", "GET", f"/api/list?v=0&from={frm}")
        if st != 200:
            check(False, f"list page {pages}: HTTP {st}")
            break
        page_json = json.loads(body)
        items += page_json["items"]
        frm = page_json["next"]
        pages += 1
    total = page_json.get("total") if pages else 0
    print(f"   All: {len(items)} figures in {pages} pages ({time.time() - t0:.1f} s); first: {items[:2]}")
    check(len(items) == total and total > 400, f"All has every figure Trap Team can use ({len(items)} of {total})")
    sections = [i[7] for i in items if i[7]]
    print("   sections:", sections[:6], "...")
    check(sections and sections[0].startswith("Gimmick Skylanders"), "All starts with the gimmick Skylanders")
    ids = [i[0] for i in items]
    check(len(set(ids)) == len(ids), "each figure once in All")
    chop = next((i for i in items if i[1] == "Chop Chop" and i[3] == "Series 1"), None) or \
        next((i for i in items if i[1] == "Chop Chop"), None)
    check(chop is not None, "Chop Chop is in the list")
    if chop:
        st, ct, body = client("pic", "GET", "/" + chop[5].replace(" ", "%20"))
        check(st == 200 and body[:4] == b"qoif", f"Chop Chop's picture comes as QOI ({st}, {len(body)} bytes)")
    # Keep-alive: the state and a phone picture over one connection.
    def keepalive():
        import http.client
        conn = http.client.HTTPConnection("127.0.0.1", HOST_PORT, timeout=60)
        conn.request("GET", "/api/state")
        r1 = conn.getresponse()
        r1.read()
        conn.request("GET", "/Phone/Cards%201/Chop%20Chop.jpg")
        r2 = conn.getresponse()
        b2 = r2.read()
        results["ka"] = (r1.status, r1.getheader("Connection"), r2.status, r2.getheader("Content-Type"), b2[:3])
        conn.close()
    before = c.net.stats["accepted"]
    t = threading.Thread(target=keepalive)
    t.start()
    while t.is_alive():
        pump()
    ka = results.get("ka")
    check(ka and ka[0] == 200 and ka[1] == "keep-alive" and ka[2] == 200 and ka[3] == "image/jpeg" and
          ka[4] == b"\xff\xd8\xff", f"keep-alive: state, then a phone picture (JPG) on the same connection ({ka})")
    check(c.net.stats["accepted"] - before == 1, "keep-alive: one connection for both")
    st, _, body = client("picks", "GET", "/api/list?v=1&from=0")
    picks = json.loads(body) if st == 200 else {}
    check(st == 200 and picks.get("items") and picks["items"][0][7].startswith("Trap Team Roster"),
          f"My Picks starts with the Trap Team roster ({str(picks.get('items', [])[:1])[:120]})")
    for name, method, path, want in [("ini", "GET", "/staticrift.ini", 404), ("dots", "GET", "/UI/../staticrift.ini", 404),
                                     ("dump", "GET", "/Figures/x.dump", 404), ("getdo", "GET", "/api/do?a=clear", 405),
                                     ("nope", "GET", "/api/nothing", 404)]:
        st, _, _ = client(name, method, path)
        check(st == want, f"{path}: {want} (got {st})")

    print("HTTP: put Hex on, take her off (and the game hears about it)")
    hex_item = next((i for i in items if i[1] == "Hex" and i[3] == "Series 1"), None) or next(i for i in items if i[1] == "Hex")
    st, _, body = client("add", "POST", f"/api/do?a=add&i={hex_item[0]}")
    r = json.loads(body) if st == 200 else {}
    check(r.get("ok") and r.get("msg") == "Hex is on the rift", f"add: {body}")
    st, _, body = client("state2", "GET", "/api/state")
    check([f[1] for f in json.loads(body)["rift"]] == ["Hex"], "state: Hex on the rift")
    st, _, body = client("again", "POST", f"/api/do?a=add&i={hex_item[0]}")
    check(json.loads(body) == {"ok": False, "msg": "Hex is already on the rift"}, f"add again: {body}")
    st, _, body = client("rm", "POST", f"/api/do?a=remove&i={hex_item[0]}")
    check(json.loads(body).get("ok"), f"remove: {body}")
    st, _, body = client("bad", "POST", "/api/do?a=add&i=99999")
    check(not json.loads(body).get("ok"), "an unknown figure is refused")
    check("Phone remote: add " in log() and "Phone remote: remove " in log(), "log: the phone's actions")

    print("HTTP: nicknames, Favorites and lists of your own")
    hid = hex_item[0]
    hex_rel = None
    st, _, body = client("st3", "GET", "/api/state")
    s3 = json.loads(body)
    check([8, "Favorites"] in s3.get("views", []) and isinstance(s3.get("nrev"), int),
          f"state: a Favorites list and the nicknames' revision ({s3.get('views')})")
    nrev0 = s3["nrev"]
    st, _, body = client("nickget", "GET", f"/api/nick?i={hid}&n=x")
    check(st == 405, "a nickname only changes by POST")
    st, _, body = client("nick", "POST", f"/api/nick?i={hid}&n=%20Hexy%20%20the%E2%80%A6%20Great%20")
    r = json.loads(body) if st == 200 else {}
    check(r.get("ok") and r.get("nick") == "Hexy the Great", f"nickname, cleaned up: {body}")
    st, _, body = client("meta", "GET", "/api/meta")
    m = json.loads(body) if st == 200 else {}
    check([hid, "Hexy the Great"] in m.get("nicks", []) and m.get("nrev", 0) > nrev0,
          f"meta: the nickname, a new revision ({body[:160]})")
    nick_ini = bytes(c.fs.read(ROOT + "nicknames.ini") or b"").decode("latin-1")
    check(" = Hexy the Great\r\n" in nick_ini, f"nicknames.ini written ({nick_ini.splitlines()[-1:]})")
    st, _, body = client("fav", "POST", f"/api/lists?a=add&l=8&i={hid}")
    check(json.loads(body).get("ok"), f"Favorites: add ({body})")
    st, _, body = client("favlist", "GET", "/api/list?v=8&from=0")
    fl = json.loads(body) if st == 200 else {}
    check([i[0] for i in fl.get("items", [])] == [hid] and fl["items"][0][7] == "Favorites",
          f"the Favorites list: Hex, one section ({body[:160]})")
    st, _, body = client("new", "POST", f"/api/lists?a=new&n=Speed%20Run&i={hid}")
    r = json.loads(body) if st == 200 else {}
    check(r.get("ok") and r.get("view") == 9, f"a new list, Hex in it ({body})")
    st, _, body = client("new2", "POST", "/api/lists?a=new&n=speed%20run")
    check(not json.loads(body).get("ok"), "a second list of that name is refused")
    st, _, body = client("ren", "POST", "/api/lists?a=rename&l=9&n=Any%25%20Run")
    check(json.loads(body).get("ok"), f"rename ({body})")
    st, _, body = client("st4", "GET", "/api/state")
    check([9, "Any% Run"] in json.loads(body).get("views", []), "state: the list, renamed")
    st, _, body = client("list9", "GET", "/api/list?v=9&from=0")
    l9 = json.loads(body) if st == 200 else {}
    check([i[0] for i in l9.get("items", [])] == [hid] and l9["items"][0][7] == "Any% Run", f"the list ({body[:120]})")
    lists_ini = bytes(c.fs.read(ROOT + "lists.ini") or b"").decode("latin-1")
    check("[Favorites]\r\n" in lists_ini and "[Any% Run]\r\n" in lists_ini and lists_ini.count("Hex") == 2,
          f"lists.ini written ({lists_ini.count(chr(10))} lines)")
    st, _, body = client("delfav", "POST", "/api/lists?a=delete&l=8")
    check(not json.loads(body).get("ok"), "Favorites can't be deleted")
    st, _, body = client("del", "POST", "/api/lists?a=delete&l=9")
    check(json.loads(body).get("ok"), f"delete ({body})")
    st, _, body = client("unfav", "POST", f"/api/lists?a=remove&l=8&i={hid}")
    check(json.loads(body).get("ok"), "Favorites: take out")
    st, _, body = client("unnick", "POST", f"/api/nick?i={hid}&n=")
    r = json.loads(body) if st == 200 else {}
    check(r.get("ok") and r.get("nick") == "", f"nickname taken away ({body})")
    st, _, body = client("meta2", "GET", "/api/meta")
    m = json.loads(body) if st == 200 else {}
    check(m.get("nicks") == [] and m.get("lists") == [[8, "Favorites", []]], f"meta: all back as it was ({body})")
    check("Phone remote: nickname " in log() and "Phone remote: new list" in log(), "log: nicknames and lists")

    # ---- a real browser ----
    node_script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "remote_browser.js")
    out_json = os.path.join(shots, "remote_browser.json")
    if os.path.exists(out_json):
        os.remove(out_json)
    print("browser: a phone-sized Chromium on the emulated Xbox's page")
    os.environ["XBOX_BLIP_FILE"] = os.path.join(shots, "blip")
    proc = subprocess.Popen(["node", node_script, f"http://127.0.0.1:{HOST_PORT}/", shots, out_json],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    deadline = time.time() + 600
    while proc.poll() is None and time.time() < deadline:
        pump()
    if proc.poll() is None:
        proc.kill()
    for line in (proc.stdout.read() or "").splitlines():
        print("   browser|", line)
    br = json.load(open(out_json)) if os.path.exists(out_json) else {}
    check(br.get("cards", 0) > 50, f"the page shows the figures ({br.get('cards')} cards)")
    check(br.get("pictures", 0) > 5, f"pictures decoded on the phone ({br.get('pictures')})")
    check(br.get("toast") == "Chop Chop is on the rift", f"the page says Chop Chop is on ({br.get('toast')})")
    check(br.get("rift") == ["Chop Chop"], f"the page's On the Rift shows him ({br.get('rift')})")
    check(br.get("errors") == [], f"no page errors ({br.get('errors')})")
    check(br.get("mini") == "1 on the rift", f"scrolled down, the bar says what's on ({br.get('mini')})")
    check(br.get("picksSections") and br["picksSections"][0].startswith("Trap Team Roster"),
          f"My Picks on the phone starts with the Trap Team roster ({br.get('picksSections')})")
    tm = br.get("timings", {})
    q = br.get("quickTap") or {}
    check(br.get("quickLit") and br.get("holdOpens") and br.get("holdAddsNothing"),
          "Quick tap: holding a figure opens it and puts nothing on")
    check(q.get("chip") and q.get("pending") and q.get("cardOn"),
          f"Quick tap: the figure shows on the rift the moment it's tapped ({q})")
    check(tm.get("quickConfirmMs") is not None, f"Quick tap: the Xbox confirms ({tm.get('quickConfirmMs')} ms here)")
    check(br.get("removeInstant"), "taking a figure off shows at once")
    check(tm.get("pushAddMs", 1e9) < 5000 and tm.get("pushRemoveMs", 1e9) < 5000,
          f"live: a change made elsewhere shows by itself ({tm.get('pushAddMs')} / {tm.get('pushRemoveMs')} ms)")
    check(br.get("pollsWhileLive") == 0, f"live: no asking while the stream is up ({br.get('pollsWhileLive')} polls)")
    check(br.get("streamReopened") and tm.get("afterBlipMs", 1e9) < 8000,
          f"Wi-Fi blip: the page reconnects by itself ({br.get('duringBlip')!r} during, change shown "
          f"{tm.get('afterBlipMs')} ms after it's back)")
    check(br.get("rift") == ["Chop Chop"], f"after all that, Chop Chop alone on the rift ({br.get('rift')})")
    cl = br.get("cardLines") or {}
    lines = cl.get("lines") or []
    check(br.get("nickBefore") == "+ Add a nickname" and br.get("nickToast") == 'Chop Chop is now "Sir Chops"',
          f"phone: a nickname given from the figure's details ({br.get('nickToast')})")
    check(len(lines) == 3 and lines[0] == "n: Chop Chop" and lines[1] == "nk: \u201cSir Chops\u201d" and
          lines[2].startswith("s: "), f"phone: the nickname between the name and the series / game ({lines})")
    check(cl.get("star") and br.get("favLit"), "phone: a favorite gets a star")
    check(br.get("nickSearch") == ["Chop Chop"], f"phone: the search finds nicknames ({br.get('nickSearch')})")
    check(br.get("favTab") == ["Chop Chop"] and "1 figure" in (br.get("favBar") or ""),
          f"phone: the Favorites tab ({br.get('favTab')}, {br.get('favBar')!r})")
    check(br.get("picker") and "[x] \u2713\u2605 Favorites1" in br["picker"] and "[x] \u2713Speedrun1" in br["picker"],
          f"phone: a new list made from the figure's Lists, the figure in it ({br.get('picker')})")
    check(br.get("renamed") and br.get("deleted"), "phone: a list renamed, then deleted")
    check(br.get("newTab", "").startswith("Nothing in this list yet"), "phone: + New list makes one and shows it")
    nick_ini = bytes(c.fs.read(ROOT + "nicknames.ini") or b"").decode("latin-1")
    lists_ini = bytes(c.fs.read(ROOT + "lists.ini") or b"").decode("latin-1")
    check("Chop Chop" in nick_ini and "= Sir Chops\r\n" in nick_ini, "nicknames.ini: Sir Chops")
    check("[Favorites]\r\n" in lists_ini and "Chop Chop" in lists_ini and "Speedrun" not in lists_ini and
          "Raid Team" not in lists_ini, "lists.ini: Chop Chop in Favorites, the deleted lists gone")

    # ---- the game sees him ----
    print("game: polls the portal and finds the figure the phone put on")

    def game_set(payload):
        buf = STACKS + 0x8000
        c.mu.mem_write(buf, bytes([0x0B, 0x14]) + bytes(payload) + bytes(30 - len(payload)))
        return c.call(setraw_thunk, [32, buf], game_stack, label="game SetRaw")

    def game_poll(cmd):
        st_p, ln_p, buf = STACKS + 0x8100, STACKS + 0x8104, STACKS + 0x8200
        for _ in range(16):
            c.put32(st_p, 0xFFFF0000)
            c.put32(ln_p, 32)
            c.call(getraw_thunk, [st_p, ln_p, buf], game_stack, label="game GetRaw")
            data = c.mem(buf, c.u32(ln_p))
            if (c.u32(st_p) >> 16) == 1 and data[2:3] == bytes([cmd]):
                return data
        return None

    game_set(b"R")
    game_poll(ord("R"))
    game_set(b"A\x01")
    game_poll(ord("A"))
    s = game_poll(ord("S"))
    check(s is not None and (s[3] & 3) in (1, 3), f"status: a figure in slot 0 ({s[:6].hex() if s else None})")
    game_set(bytes([ord("Q"), 0x20, 1]))
    q = game_poll(ord("Q"))
    check(q is not None and q[3] == 0x10, "the game reads the figure's blocks")
    state_txt = bytes(c.fs.read(ROOT + "rift_state.txt") or b"").decode()
    check("Chop Chop" in state_txt, f"rift_state.txt remembers Chop Chop ({state_txt.strip()!r})")

    print("game: Swap in from the phone - how soon the game sees the new figure")

    def swap_timing(fid, name):
        """POSTs a swap, then the game polls the portal (one status report a poll, as it does
        every frame) until a figure arrives; returns (polls, seconds, removal seen first)."""
        t0 = time.time()
        st, _, body = client("swap_" + name, "POST", f"/api/do?a=swap&i={fid}")
        if st != 200 or not json.loads(body).get("ok"):
            return None
        removed_first = False
        for n in range(1, 400):
            r = game_poll(ord("S"))
            bits = int.from_bytes(r[3:7], "little") if r else 0
            states = [(bits >> (2 * k)) & 3 for k in range(16)]
            if 2 in states:
                removed_first = True
            if 3 in states:
                return n, time.time() - t0, removed_first
            pump()  # (the worker runs between the game's polls)
        return None

    t_hex = swap_timing(hex_item[0], "hex")
    t_chop = swap_timing(chop[0], "chop")
    print(f"   Hex in for Chop Chop: {t_hex}; Chop Chop back: {t_chop} (polls, seconds here, removal first)")
    check(t_hex is not None and t_hex[2] and t_hex[0] <= 20,
          f"swap: the old figure comes off first, the new one arrives a few polls later ({t_hex})")
    check(t_chop is not None and t_chop[2] and t_chop[0] <= 20, f"swap back ({t_chop})")
    for line in log().splitlines()[-6:]:
        print("   log|", line)

    if rgh:
        print("RGH: quit to the dashboard - the game's threads and network go, the remote with them")
        c.title_id = 0xFFFE07D1
        c.put32(rd.XEXEC_HANDLE_VAR, 0)
        for h in list(c.net.socks):  # the game's network stack is torn down
            try:
                c.net.socks[h].close()
            except OSError:
                pass
        c.net.socks.clear()
        c.net.accepted.clear()
        for _ in range(60):  # (the server thread isn't run again: it ended with the game)
            c.run_thread(worker, 2)
            if "Phone remote: stopped with the game" in log():
                break
        check("Title 41560911 ended" in log() and "Phone remote: stopped with the game" in log(),
              "the game's exit noticed; the remote stopped with it")
        server = None
        st, _, body = client("gone", "GET", "/api/state")
        check(st == 0, f"nothing answers on the dashboard ({st})")
        print("RGH: Trap Team starts again - the remote starts again with it")
        c.build_game()
        c.title_id = 0x41560911
        c.put32(rd.XEXEC_HANDLE_VAR, rd.GAME_LDR)
        c.run_thread(worker, 3)
        check(log().count("Phone remote: starting with the game") == 2, "the remote asks to start with the new game")
        server = server_from_game()
        if server is None:
            return finish()
        for _ in range(200):
            pump()
            if log().count("Phone remote: listening") == 2:
                break
        check(log().count("Phone remote: listening - open http://192.168.1.23") == 2, "listening again")
        pump(3)
        check(sum(t == "Phone remote: open http://192.168.1.23" for t in c.toasts) == 2,
              "the address pop-up again, for this game")
        st, _, body = client("again", "GET", "/api/state")
        s5 = json.loads(body) if st == 200 else {}
        check(st == 200 and s5.get("gameName") == "Trap Team", f"the phone can use it again ({st})")
        st, _, body = client("again2", "POST", f"/api/do?a=add&i={hex_item[0]}")
        check(st == 200 and json.loads(body).get("ok"), f"and put a figure on ({body[:80]})")
    return finish()


def run_load(c, pump, shots, log):
    """A phone using the page for a while, timed: first screen, scrolling, My Picks, then idle."""
    script = os.environ.get("REMOTE_SCRIPT", "remote_load.js")
    node_script = os.path.join(os.path.dirname(os.path.abspath(__file__)), script)
    out_json = os.path.join(shots, script.replace(".js", ".json"))
    if os.path.exists(out_json):
        os.remove(out_json)
    t0 = time.time()
    proc = subprocess.Popen(["node", node_script, f"http://127.0.0.1:{HOST_PORT}/", shots, out_json],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    while proc.poll() is None and time.time() - t0 < 900:
        pump()
    if proc.poll() is None:
        proc.kill()
    for line in (proc.stdout.read() or "").splitlines():
        print("   load|", line)
    result = json.load(open(out_json)) if os.path.exists(out_json) else {}
    result["net"] = c.net.stats
    result["limits"] = {"on": c.net.limits, "pool": c.net.pool, "time_wait": c.net.time_wait, "rate": c.net.rate}
    print("LOAD RESULT", json.dumps(result))
    json.dump(result, open(out_json, "w"), indent=1)
    for line in log().splitlines():
        if "Phone remote" in line:
            print("   log|", line)
    return 0


def finish():
    if rd.failures:
        print(f"{len(rd.failures)} check(s) failed")
        return 1
    print("all phone remote checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
