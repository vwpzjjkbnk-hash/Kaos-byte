#!/usr/bin/env python3
"""End-to-end test of the built SkyPortal360.dll on an emulated CPU.

Runs the DLL's real PowerPC code on Unicorn's PowerPC 970 model in 32-bit mode with 64-bit
registers (MSR[SF]=0), which is how the Xbox 360 runs title code: 64-bit instructions work and
effective addresses use the low 32 bits. A fake loader, kernel, XAM, filesystem and Skylanders
game stand in for the console:

  - the XEX is parsed and its imports resolved the way the loader does (export addresses into
    variable slots, `lis r11 / ori r11` into thunks);
  - every kernel/XAM call is checked against the console's convention (arguments zero-extended,
    a 9th argument at 0x54(r1) with 0x50(r1) zero), and each fake export then behaves like an
    unfriendly MSVC callee: it scribbles over its caller's argument slots 0x10-0x57(r1), the stack
    below r1, and every volatile register's full 64 bits;
  - the game calls the hooks through its own (patched) import stubs with 64-bit values in
    r14-r31, which must come back whole.

Usage: run_dll.py <plugin>.xex <plugin>.elf [classic|pop|drawn|rgh]   (needs Unicorn's Python
binding; set LIBUNICORN_PATH to the directory with libunicorn.so.2). The variant picks the menu
checks: notification toasts, the skypop message box, or the skydrawn overlay (whose rendered
front buffer is also saved as emu_drawn.png next to the .xex for a look). rgh: the Dashlaunch
build (drawn menu) loaded at boot on the dashboard; the game is launched, played with the menu,
quit to the dashboard and launched again.
"""

import struct
import sys

try:
    from unicorn import Uc, UcError, UC_ARCH_PPC, UC_MODE_PPC64, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE
    from unicorn.ppc_const import *
except ImportError:
    sys.exit("run_dll.py: needs Unicorn's Python binding (unicorn-engine.org)")

# ------------------------------------------------------------------------------------------------
# Memory layout of the fake console
# ------------------------------------------------------------------------------------------------
KERNEL = 0x80100000      # fake exports: KERNEL + library*0x1000 + ordinal*4
KVARS = 0x80110000       # fake kernel variables
RETURN_MAGIC = 0x8011F000  # returning here ends a call()
PCR = 0x801B0000         # r13 points at a processor control region on the console
GAME = 0x82000000        # fake game image
GAME_LDR = GAME + 0x8000
GAME_XEX_HEADER = GAME + 0x9000
GAME_IMPORTS = GAME + 0xA000
GAME_EXEC_ID = GAME + 0xB000
STACKS = 0x70000000      # thread stacks, 64 KB each
GPU_REGS = 0x7FC80000    # skydrawn reads the display controller's scan-out address here
DISPLAY_ADDRESS_REG = GPU_REGS + 0x1844 * 4
FLIP_CONTROL_REG = GPU_REGS + 0x1852 * 4
UPDATE_REG = GPU_REGS + 0x1851 * 4         # bit 16: hold the surface registers (update lock)   # bit 0: take a new scan-out address at the next line
FRONT_BUFFERS = 0xB0000000  # two 1280x720 tiled 32 bpp front buffers (4 MB apart)
FB_W, FB_H, FB_PITCH = 1280, 720, 1280
PHYS_POOL = 0xA0000000   # MmAllocatePhysicalMemoryEx hands out memory from here
PHYS_POOL_SIZE = 0x2000000
VIRT_POOL = 0x40000000   # NtAllocateVirtualMemory hands out memory from here (4 KB pages)
VIRT_POOL_SIZE = 0x1000000

LIB_XAM, LIB_KRNL = 0, 1
LIB_INDEX = {"xam.xex": LIB_XAM, "xboxkrnl.exe": LIB_KRNL}

def export_address(library, ordinal):
    return KERNEL + library * 0x1000 + ordinal * 4

XAM_GETRAW, XAM_SETRAW, XAM_GETSTATE, XAM_TITLEID = 0x4A1, 0x4A2, 0x191, 0x1CF
XEXEC_HANDLE_VAR = KVARS + 0x10  # XexExecutableModuleHandle: holds GAME_LDR

STATUS_SUCCESS = 0
STATUS_NAME_NOT_FOUND = 0xC0000034
STATUS_NAME_COLLISION = 0xC0000035
STATUS_PATH_NOT_FOUND = 0xC000003A
STATUS_NO_SUCH_FILE = 0xC000000F
STATUS_NO_MORE_FILES = 0x80000006
STATUS_NOT_A_DIRECTORY = 0xC0000103
STATUS_INVALID_HANDLE = 0xC0000008

failures = []

def check(cond, what):
    if not cond:
        failures.append(what)
        print("  FAIL", what)
    return cond

# ------------------------------------------------------------------------------------------------
# A second, independent XEX parser (the loader's view)
# ------------------------------------------------------------------------------------------------
def parse_xex(data):
    magic, flags, header_size, _, security_offset, count = struct.unpack_from(">4sIIIII", data, 0)
    assert magic == b"XEX2", "not a XEX2"
    headers = {}
    for i in range(count):
        key, value = struct.unpack_from(">II", data, 0x18 + 8 * i)
        headers[key] = value
    image_size = struct.unpack_from(">I", data, security_offset + 4)[0]
    load_address = struct.unpack_from(">I", data, security_offset + 0x110)[0]
    fmt = headers[0x3FF]
    fmt_size, encryption, compression = struct.unpack_from(">IHH", data, fmt)
    assert encryption in (0, 1) and compression == 1, "expected basic (uncompressed) blocks"
    runs = [struct.unpack_from(">II", data, fmt + 8 + 8 * i) for i in range((fmt_size - 8) // 8)]
    stored = sum(d for d, _ in runs)
    image = bytearray(data[header_size:header_size + stored])
    if encryption == 1:  # devkit key (zeros) -> image key -> AES-CBC, zero IV
        import subprocess
        def aes(mode, key, blob):
            cmd = ["openssl", "enc", "-d", "-aes-128-" + mode, "-K", key.hex(), "-nopad"]
            if mode == "cbc":
                cmd += ["-iv", "00" * 16]
            return subprocess.run(cmd, input=bytes(blob), capture_output=True, check=True).stdout
        image_key = aes("ecb", bytes(16), data[security_offset + 0x150:security_offset + 0x160])
        image = bytearray(aes("cbc", image_key, image))
        assert image[:2] == b"MZ", "devkit decryption didn't produce the PE image"
    expanded, p = bytearray(), 0
    for d, z in runs:
        expanded += image[p:p + d] + bytes(z)
        p += d
    image = expanded
    assert len(image) == image_size, "blocks don't add up to the image size"
    # import libraries
    off = headers[0x103FF]
    size, names_size, lib_count = struct.unpack_from(">III", data, off)
    names_blob = data[off + 12:off + 12 + names_size]
    names = [n.decode() for n in names_blob.split(b"\0") if n]
    libs = []
    p = off + 12 + names_size
    for i in range(lib_count):
        rec_size = struct.unpack_from(">I", data, p)[0]
        import_id, version, min_version, index, n = struct.unpack_from(">IIIHH", data, p + 24)
        records = list(struct.unpack_from(f">{n}I", data, p + 40))
        libs.append({"name": names[index], "records": records})
        p += rec_size
    # Pages: the loader maps the image in 4 KB pages when the image flags say so, else 64 KB, and
    # the address decides which: 0x80000000-0x8FFFFFFF is the 64 KB-page image region,
    # 0x90000000-0x9FFFFFFF (system DLLs, Dashlaunch plugins) the 4 KB one. Each page descriptor's
    # SHA-1 covers its pages and the next descriptor; the security info's covers the first.
    import hashlib
    image_flags = struct.unpack_from(">I", data, security_offset + 0x10C)[0]
    page = 0x1000 if image_flags & 0x10000000 else 0x10000
    base = headers[0x10201]
    want = 0x1000 if 0x90000000 <= base < 0xA0000000 else 0x10000
    check(page == want, f"image pages are {page // 1024} KB, as the region at {base:#x} needs {want // 1024} KB")
    count_d = struct.unpack_from(">I", data, security_offset + 0x180)[0]
    descs = [struct.unpack_from(">I20s", data, security_offset + 0x184 + 0x18 * i) for i in range(count_d)]
    digest = data[security_offset + 0x114:security_offset + 0x128]
    pos, chain_ok = 0, True
    for i, (info, next_digest) in enumerate(descs):
        n = info >> 4
        raw = data[security_offset + 0x184 + 0x18 * i:security_offset + 0x184 + 0x18 * (i + 1)]
        h = hashlib.sha1(bytes(image[pos:pos + n * page]) + raw).digest()
        if h != digest:
            chain_ok = False
            break
        digest, pos = next_digest, pos + n * page
    check(chain_ok and pos == image_size, f"page descriptors ({count_d}) hash-chain over the whole image "
          f"in {page // 1024} KB pages")
    return {
        "flags": flags, "base": headers[0x10201], "entry": headers[0x10100], "image": image,
        "load_address": load_address, "libs": libs,
    }

def elf_symbols(path):
    data = open(path, "rb").read()
    eh = struct.unpack_from(">16sHHIIIIIHHHHHH", data)
    shoff, shnum = eh[6], eh[12]
    secs = [struct.unpack_from(">IIIIIIIIII", data, shoff + i * 40) for i in range(shnum)]
    symbols = {}
    for s in secs:
        if s[1] != 2:  # SHT_SYMTAB
            continue
        strtab = secs[s[6]]
        for i in range(s[5] // 16):
            name, value, size, info, other, shndx = struct.unpack_from(">IIIBBH", data, s[4] + i * 16)
            end = data.index(b"\0", strtab[4] + name)
            symbols[data[strtab[4] + name:end].decode()] = value
    return symbols

# ------------------------------------------------------------------------------------------------
# The emulator
# ------------------------------------------------------------------------------------------------
class Console:
    def __init__(self, xex, symbols):
        self.xex, self.sym = xex, symbols
        mu = self.mu = Uc(UC_ARCH_PPC, UC_MODE_PPC64 | UC_MODE_BIG_ENDIAN)
        mu.ctl_set_cpu_model(UC_CPU_PPC64_970FX_V3_1)
        base = xex["base"]
        mu.mem_map(base, len(xex["image"]))
        mu.mem_write(base, bytes(xex["image"]))
        mu.mem_map(KERNEL, 0x20000)
        mu.mem_map(PCR, 0x10000)
        mu.mem_write(PCR + 0x100, struct.pack(">I", PCR + 0x1000))  # PCR->CurrentThread
        mu.mem_map(GAME, 0x10000)
        mu.mem_map(STACKS, 0x100000)
        mu.mem_map(GPU_REGS, 0x10000)
        mu.mem_map(FRONT_BUFFERS, 0x800000)
        mu.mem_map(PHYS_POOL, PHYS_POOL_SIZE)
        # Physical address 0 is never handed out on the console (it means "none" to the menu's
        # scan-out pin), so the pool's first allocation must not land there.
        self.phys_next = PHYS_POOL + 0x10000
        mu.mem_map(VIRT_POOL, VIRT_POOL_SIZE)
        self.virt_next, self.virt_allocs, self.virt_frees = VIRT_POOL, {}, []
        self.phys_allocs, self.phys_frees = {}, []
        # fake exports: a trap at each (never executed: the hook returns to LR first)
        mu.mem_write(KERNEL, struct.pack(">I", 0x7FE00008) * (0x2000 // 4))
        mu.mem_write(XEXEC_HANDLE_VAR, struct.pack(">I", GAME_LDR))
        self.write_msr_32bit()
        self.violations = []
        self.calls = {}
        self.toasts, self.debug = [], []
        self.vdswaps, self.boxes = [], []
        self.gfx_cb, self.priorities = None, []
        self.threads = []
        self.time = 132000000000000000  # 100 ns units
        self.sleeps = 0
        self.yield_after = None
        self.pad = {"buttons": 0, "lt": 0, "rt": 0, "packet": 1}
        self.title_id = 0x41560911
        self.system_dll = False
        self.current_thread = None
        self.fs = FakeFs()
        self.links = {}
        self.handles = {}
        self.next_handle = 0x100
        self.call_depth = 0
        mu.hook_add(UC_HOOK_CODE, self.on_export, begin=KERNEL, end=KERNEL + 0x1FFFF)
        self.resolve_imports()
        self.build_game()

    # ---- registers / memory helpers ----
    def write_msr_32bit(self):
        self.mu.reg_write(UC_PPC_REG_MSR, self.mu.reg_read(UC_PPC_REG_MSR) & ~(1 << 63))

    def r(self, n):
        return self.mu.reg_read(UC_PPC_REG_0 + n)

    def w(self, n, v):
        self.mu.reg_write(UC_PPC_REG_0 + n, v & 0xFFFFFFFFFFFFFFFF)

    def u32(self, addr):
        return struct.unpack(">I", self.mu.mem_read(addr & 0xFFFFFFFF, 4))[0]

    def put32(self, addr, v):
        self.mu.mem_write(addr & 0xFFFFFFFF, struct.pack(">I", v & 0xFFFFFFFF))

    def put64(self, addr, v):
        self.mu.mem_write(addr & 0xFFFFFFFF, struct.pack(">Q", v & 0xFFFFFFFFFFFFFFFF))

    def mem(self, addr, n):
        return bytes(self.mu.mem_read(addr & 0xFFFFFFFF, n))

    def wstr(self, addr):
        raw = b""
        while addr and len(raw) < 2048:
            ch = self.mem(addr + len(raw), 2)
            if ch == b"\0\0":
                break
            raw += ch
        return raw.decode("utf-16-be")

    def cstr(self, addr):
        out = b""
        while True:
            b = self.mem(addr + len(out), 64)
            i = b.find(b"\0")
            if i >= 0:
                return (out + b[:i]).decode("latin-1")
            out += b

    def ansi(self, addr):  # STRING {u16 len, u16 max, char* buffer}
        length, _, buf = struct.unpack(">HHI", self.mem(addr, 8))
        return self.mem(buf, length).decode("latin-1")

    # ---- the loader ----
    def resolve_imports(self):
        base = self.xex["base"]
        for lib in self.xex["libs"]:
            index = LIB_INDEX[lib["name"]]
            for addr in lib["records"]:
                word = self.u32(addr)
                kind, ordinal = word >> 24, word & 0xFFFF
                check((word >> 16) & 0xFF == self.xex["libs"].index(lib),
                      f"record {addr:#x} carries its library index")
                target = export_address(index, ordinal)
                if kind == 0:
                    value = XEXEC_HANDLE_VAR if (index, ordinal) == (LIB_KRNL, 0x193) else target
                    self.put32(addr, value)
                elif kind == 1:
                    self.put32(addr, 0x3D600000 | (target >> 16))      # lis r11, hi
                    self.put32(addr + 4, 0x616B0000 | (target & 0xFFFF))  # ori r11, r11, lo
                else:
                    check(False, f"unknown import record type {kind} at {addr:#x}")

    # ---- the fake game: an import table with xam's portal + input imports ----
    def build_game(self):
        self.game_imports = {}  # ordinal -> (var, thunk)
        entries = [(LIB_XAM, XAM_TITLEID), (LIB_XAM, XAM_GETSTATE), (LIB_XAM, XAM_GETRAW),
                   (LIB_XAM, XAM_SETRAW), (LIB_KRNL, 0x0CF), (LIB_KRNL, 0x197), (LIB_KRNL, 0x25B),
                   (LIB_KRNL, 0x1D5)]
        self.krnl_imports = {}
        var, thunk = GAME + 0x1000, GAME + 0x2000
        per_lib = {LIB_XAM: [], LIB_KRNL: []}
        for lib, ordinal in entries:
            target = export_address(lib, ordinal)
            self.put32(var, target)
            self.mu.mem_write(thunk, struct.pack(">IIII", 0x3D600000 | (target >> 16),
                                                 0x616B0000 | (target & 0xFFFF), 0x7D6903A6, 0x4E800420))
            per_lib[lib].append((var, thunk))
            if lib == LIB_XAM:
                self.game_imports[ordinal] = (var, thunk)
            else:
                self.krnl_imports[ordinal] = (var, thunk)
            var += 4
            thunk += 16
        # XEX import descriptor: header, name table, then one table per module
        names = b"xam.xex\0xboxkrnl.exe\0\0\0\0"
        blob = bytearray(struct.pack(">III", 0, len(names), 2) + names)
        for lib in (LIB_XAM, LIB_KRNL):
            records = [a for pair in per_lib[lib] for a in pair]
            table = struct.pack(">I", 40 + 4 * len(records)) + bytes(20) + struct.pack(
                ">IIIBBH", 0, 0, 0, 0, lib, len(records)) + struct.pack(f">{len(records)}I", *records)
            blob += table
        struct.pack_into(">I", blob, 0, len(blob))
        self.mu.mem_write(GAME_IMPORTS, bytes(blob))
        self.mu.mem_write(GAME_EXEC_ID, struct.pack(">IIIIBBBBI", 0x12345678, 0x00000501, 0,
                                                   0x41560911, 0, 0, 1, 1, 0))
        self.put32(GAME_LDR + 0x58, GAME_XEX_HEADER)

    # ---- calling guest code ----
    def call(self, addr, args, stack_top, check_regs=True, label="", stack_args=()):
        mu = self.mu
        for i in range(3, 11):
            self.w(i, args[i - 3] if i - 3 < len(args) else 0)
        sp = (stack_top - 0x200) & ~0xF
        self.put32(sp, 0)  # back chain
        for i, v in enumerate(stack_args):  # arguments 9+ in the MSVC slots at 0x50(r1)
            self.put32(sp + 0x50 + 8 * i, 0)
            self.put32(sp + 0x54 + 8 * i, v)
        self.w(1, sp)
        self.w(2, 0)
        self.w(13, PCR)
        pattern = {n: (0xA5A50000 + n) << 32 | (0x10000000 + n) for n in range(14, 32)}
        for n, v in pattern.items():
            self.w(n, v)
        mu.reg_write(UC_PPC_REG_LR, RETURN_MAGIC)
        self.write_msr_32bit()
        mu.emu_start(addr, RETURN_MAGIC)
        if check_regs:
            kept = all(self.r(n) == v for n, v in pattern.items())
            check(kept, f"{label}: r14-r31 kept whole across the call")
            check(self.r(1) == sp, f"{label}: r1 restored")
            check(self.r(2) == 0 and self.r(13) == PCR, f"{label}: r2/r13 untouched")
        return self.r(3) & 0xFFFFFFFF

    def call_start(self, addr, args, stack_top, sleeps, stack_args=()):
        """Like call(), for a call that may block: runs until it returns or has slept `sleeps`
        times, and returns a handle for call_resume() (the game's VdSwap waits like this while
        the menu has the game paused)."""
        mu = self.mu
        for i in range(3, 11):
            self.w(i, args[i - 3] if i - 3 < len(args) else 0)
        sp = (stack_top - 0x200) & ~0xF
        self.put32(sp, 0)
        for i, v in enumerate(stack_args):
            self.put32(sp + 0x50 + 8 * i, 0)
            self.put32(sp + 0x54 + 8 * i, v)
        self.w(1, sp)
        self.w(2, 0)
        self.w(13, PCR)
        mu.reg_write(UC_PPC_REG_LR, RETURN_MAGIC)
        self.write_msr_32bit()
        t = {"call": True}
        return self._run_call(t, addr, sleeps)

    def call_resume(self, t, sleeps):
        if t["done"]:
            return t
        self.mu.context_restore(t["context"])
        return self._run_call(t, self.mu.reg_read(UC_PPC_REG_PC), sleeps)

    def _run_call(self, t, pc, sleeps):
        self.current_thread = t
        self.yield_after = self.sleeps + sleeps
        self.mu.emu_start(pc, RETURN_MAGIC)
        self.yield_after = None
        t["done"] = self.mu.reg_read(UC_PPC_REG_PC) == RETURN_MAGIC
        if not t["done"]:
            t["context"] = self.mu.context_save()
        return t

    def run_thread(self, thread, sleeps):
        """Runs (or resumes) a created thread until it has slept `sleeps` more times."""
        mu = self.mu
        self.yield_after = self.sleeps + sleeps
        if "context" not in thread:
            sp = thread["stack"] - 0x100
            self.put32(sp, 0)
            self.w(1, sp)
            self.w(2, 0)
            self.w(13, PCR)
            self.w(3, thread["start"])
            self.w(4, thread["param"])
            mu.reg_write(UC_PPC_REG_LR, RETURN_MAGIC)
            self.write_msr_32bit()
            pc = thread["startup"]
        else:
            mu.context_restore(thread["context"])
            pc = mu.reg_read(UC_PPC_REG_PC)
        self.current_thread = thread
        mu.emu_start(pc, RETURN_MAGIC)
        thread["context"] = mu.context_save()
        self.yield_after = None

    # ---- fake exports ----
    def on_export(self, uc, addr, size, user):
        if addr == RETURN_MAGIC:
            uc.emu_stop()
            return
        offset = addr - KERNEL
        lib, ordinal = offset // 0x1000, (offset % 0x1000) // 4
        name, nargs, fn = EXPORTS.get((lib, ordinal), (None, 0, None))
        if fn is None:
            check(False, f"call to unexpected export lib {lib} ordinal {ordinal:#x}")
            uc.emu_stop()
            return
        self.calls[name] = self.calls.get(name, 0) + 1
        sp = self.r(1) & 0xFFFFFFFF
        args = []
        for i in range(min(nargs, 8)):
            v = self.r(3 + i)
            if v >> 32 and not (name == "XNotifyQueueUI" and i == 2 and v >> 32 == 0):
                self.violations.append(f"{name} arg {i + 1} not zero-extended: {v:#x}")
            args.append(v & 0xFFFFFFFF)
        for extra in range(8, nargs):  # 9th and later: 8-byte slots from 0x50(r1)
            hi, lo = self.u32(sp + 0x50 + 8 * (extra - 8)), self.u32(sp + 0x54 + 8 * (extra - 8))
            if hi:
                self.violations.append(f"{name} argument {extra + 1} slot high word {hi:#x}")
            args.append(lo)
        result = fn(self, *args)
        lr = uc.reg_read(UC_PPC_REG_LR)
        # Behave like an MSVC callee that uses everything it may: the caller's argument slots,
        # the stack below r1 (its own saves), and all volatile registers' full 64 bits.
        self.mu.mem_write(sp + 0x10, b"\xCC" * 0x48)
        self.mu.mem_write(sp - 0x200, b"\xEE" * 0x200)
        for n in (0, 4, 5, 6, 7, 8, 9, 10, 11, 12):
            self.w(n, 0xDEADBEEF00000000 | (n * 0x01010101))
        uc.reg_write(UC_PPC_REG_CTR, 0xBADC0FFEE0DDF00D)
        self.w(3, (result or 0) & 0xFFFFFFFF)
        if getattr(self, "stop_now", False):
            self.stop_now = False
            uc.reg_write(UC_PPC_REG_PC, lr)
            uc.emu_stop()
            return
        uc.reg_write(UC_PPC_REG_PC, lr)

    # ---- file helpers for the fake kernel ----
    def resolve_path(self, oa):
        root = self.u32(oa)
        name = self.ansi(self.u32(oa + 4))
        if root == 0xFFFFFFFD:  # OB_DOS_DEVICES
            drive, _, rest = name.partition(":")
            device = self.links.get(drive.upper() + ":")
            if device is None:
                return None
            return self.fs.device_path(device, rest)
        return self.fs.device_path(name, "")

# ------------------------------------------------------------------------------------------------
# A small in-memory filesystem
# ------------------------------------------------------------------------------------------------
class FakeFs:
    DEVICES = {"\\device\\harddisk0\\partition1": "hdd"}

    def __init__(self):
        self.nodes = {"hdd": None}  # path -> None for directories, bytearray for files

    def device_path(self, device, rest):
        root = self.DEVICES.get(device.lower())
        if root is None:  # a full kernel path: "\Device\Harddisk0\Partition1\file"
            for dev, r in self.DEVICES.items():
                if device.lower().startswith(dev + "\\"):
                    root, rest = r, device[len(dev):] + rest
                    break
        if root is None:
            return None
        parts = [p for p in rest.split("\\") if p]
        return "/".join([root] + parts)

    def key(self, path):
        for k in self.nodes:
            if k.lower() == path.lower():
                return k
        return None

    def parent_exists(self, path):
        parent = path.rsplit("/", 1)[0]
        k = self.key(parent)
        return k is not None and self.nodes[k] is None

    def children(self, path):
        prefix = path.lower() + "/"
        out = []
        for k, v in self.nodes.items():
            if k.lower().startswith(prefix) and "/" not in k[len(prefix):]:
                out.append((k[len(prefix):], v))
        return sorted(out, key=lambda e: e[0].lower())

    def mkdirs(self, path):
        parts = path.split("/")
        for i in range(1, len(parts) + 1):
            p = "/".join(parts[:i])
            if self.key(p) is None:
                self.nodes[p] = None

    def write(self, path, data):
        self.mkdirs(path.rsplit("/", 1)[0])
        self.nodes[path] = bytearray(data)

    def read(self, path):
        k = self.key(path)
        return None if k is None else self.nodes[k]

# ------------------------------------------------------------------------------------------------
# Fake kernel and XAM exports: (library, ordinal) -> (name, argument count, implementation)
# ------------------------------------------------------------------------------------------------
def k_dbgprint(c, fmt, arg, *rest):
    f = c.cstr(fmt)
    c.debug.append(c.cstr(arg) if f == "%s" else f)
    return 0

def k_create_thread(c, handle_p, stack, tid_p, startup, start, param, flags):
    top = STACKS + 0x10000 * (len(c.threads) + 2)
    t = {"startup": startup, "start": start, "param": param, "flags": flags, "stack": top}
    c.threads.append(t)
    c.put32(handle_p, 0x2000 + len(c.threads))
    if tid_p:
        c.put32(tid_p, 0x40 + len(c.threads))
    return 0

def k_terminate_thread(c, code):
    c.current_thread["exited"] = code
    c.stop_now = True
    return 0

def k_delay(c, mode, alertable, interval_p):
    interval = struct.unpack(">q", c.mem(interval_p, 8))[0]
    check(interval <= 0, "KeDelayExecutionThread gets a relative (negative) interval")
    c.time += -interval
    c.sleeps += 1
    if c.yield_after is not None and c.sleeps >= c.yield_after:
        c.stop_now = True
    return 0

def k_query_time(c, time_p):
    c.time += 10000  # 1 ms per query, plus the sleeps
    c.put64(time_p, c.time)
    return 0

def k_close(c, h):
    return STATUS_SUCCESS if c.handles.pop(h, None) is not None else STATUS_INVALID_HANDLE

def k_create_file(c, handle_p, access, oa, iosb, alloc, attrs, share, disposition, options):
    check(access & 0x00100000, "NtCreateFile asks for SYNCHRONIZE")
    check(options & 0x20, "NtCreateFile asks for synchronous I/O")
    path = c.resolve_path(oa)
    c.put32(handle_p, 0)
    if path is None:
        return STATUS_PATH_NOT_FOUND
    k = c.fs.key(path)
    want_dir = bool(options & 1)
    if k is not None:
        if disposition == 2:  # FILE_CREATE
            status = STATUS_NAME_COLLISION
        elif want_dir and c.fs.nodes[k] is not None:
            status = STATUS_NOT_A_DIRECTORY
        else:
            if disposition in (4, 5) and c.fs.nodes[k] is not None:  # overwrite
                c.fs.nodes[k] = bytearray()
            status = STATUS_SUCCESS
    else:
        if disposition in (1, 4):  # FILE_OPEN / FILE_OVERWRITE
            status = STATUS_NAME_NOT_FOUND
        elif not c.fs.parent_exists(path):
            status = STATUS_PATH_NOT_FOUND
        else:
            c.fs.nodes[path] = None if want_dir else bytearray()
            k = path
            status = STATUS_SUCCESS
    if status == STATUS_SUCCESS:
        h = c.next_handle
        c.next_handle += 4
        c.handles[h] = {"path": k, "pos": 0, "scan": None}
        c.put32(handle_p, h)
    c.put32(iosb, status)
    c.put32(iosb + 4, 0)
    return status

def k_read_file(c, h, event, apc, ctx, iosb, buf, length, offset_p):
    f = c.handles.get(h)
    if not f:
        return STATUS_INVALID_HANDLE
    data = c.fs.nodes[f["path"]]
    chunk = bytes(data[f["pos"]:f["pos"] + length])
    c.mu.mem_write(buf, chunk)
    f["pos"] += len(chunk)
    c.put32(iosb, 0)
    c.put32(iosb + 4, len(chunk))
    return 0

def k_write_file(c, h, event, apc, ctx, iosb, buf, length, offset_p):
    f = c.handles.get(h)
    if not f:
        return STATUS_INVALID_HANDLE
    data = c.fs.nodes[f["path"]]
    chunk = c.mem(buf, length)
    end = f["pos"] + length
    if len(data) < end:
        data.extend(bytes(end - len(data)))
    data[f["pos"]:end] = chunk
    f["pos"] = end
    c.put32(iosb, 0)
    c.put32(iosb + 4, length)
    return 0

def k_flush(c, h, iosb):
    return 0 if h in c.handles else STATUS_INVALID_HANDLE

def network_open_info(attributes, size):
    return struct.pack(">QQQQQQII", 0, 0, 0, 0, size, size, attributes, 0)

def k_query_info(c, h, iosb, info, length, cls):
    f = c.handles.get(h)
    if not f:
        return STATUS_INVALID_HANDLE
    data = c.fs.nodes[f["path"]]
    if cls == 34:
        c.mu.mem_write(info, network_open_info(0x10 if data is None else 0x80, len(data or b"")))
    elif cls == 14:
        c.put64(info, f["pos"])
    else:
        return 0xC0000003
    return 0

def k_set_info(c, h, iosb, info, length, cls):
    f = c.handles.get(h)
    if not f or cls != 14:
        return 0xC0000003
    f["pos"] = struct.unpack(">Q", c.mem(info, 8))[0]
    return 0

def k_query_full_attributes(c, oa, info):
    path = c.resolve_path(oa)
    k = c.fs.key(path) if path else None
    if k is None:
        return STATUS_NAME_NOT_FOUND
    data = c.fs.nodes[k]
    c.mu.mem_write(info, network_open_info(0x10 if data is None else 0x80, len(data or b"")))
    return 0

def k_query_directory(c, h, event, apc, ctx, iosb, info, length, pattern_p, restart):
    f = c.handles.get(h)
    if not f:
        return STATUS_INVALID_HANDLE
    if restart or f["scan"] is None:
        check(pattern_p != 0, "first NtQueryDirectoryFile call passes a pattern")
        f["scan"] = c.fs.children(f["path"])
        if not f["scan"]:
            return STATUS_NO_SUCH_FILE
    if not f["scan"]:
        return STATUS_NO_MORE_FILES
    # Up to two entries per call, chained, to exercise both the chained and one-at-a-time paths.
    out, take = b"", f["scan"][:2]
    f["scan"] = f["scan"][2:]
    for i, (name, data) in enumerate(take):
        nb = name.encode()
        entry = struct.pack(">IIQQQQQQII", 0, 0, 0, 0, 0, 0, len(data or b""), len(data or b""),
                            0x10 if data is None else 0x80, len(nb)) + nb
        entry += bytes((-len(entry)) % 8)
        if i + 1 < len(take):
            entry = struct.pack(">I", len(entry)) + entry[4:]
        out += entry
    check(len(out) <= length, "directory entries fit the caller's buffer")
    c.mu.mem_write(info, out)
    return 0

def k_create_link(c, link_p, device_p):
    link, device = c.ansi(link_p), c.ansi(device_p)
    system = link.startswith("\\System??\\")
    on_game_thread = not (c.current_thread or {}).get("flags", 2) & 2
    want_system = c.system_dll and not on_game_thread
    check(system == want_system, f"{'system-wide' if want_system else 'title-local'} link from "
          f"{'a system' if not on_game_thread else 'a game'} thread ({link})")
    prefix = "\\System??\\" if system else "\\??\\"
    c.links[link[len(prefix):].upper()] = device
    return 0

def k_delete_link(c, link_p):
    link = c.ansi(link_p)
    prefix = "\\System??\\" if c.system_dll else "\\??\\"
    return 0 if c.links.pop(link[len(prefix):].upper(), None) else 0xC0000034

def k_cs(c, cs):
    check(cs % 4 == 0, "critical section aligned")
    return 0

def k_header_field(c, header, key):
    if header != GAME_XEX_HEADER:
        return 0
    return {0x000103FF: GAME_IMPORTS, 0x00040006: GAME_EXEC_ID}.get(key, 0)

def k_init_ansi(c, dest, src):
    n = len(c.cstr(src)) if src else 0
    c.mu.mem_write(dest, struct.pack(">HHI", n, n + 1 if src else 0, src))
    return 0

def k_module_handle(c, name_p, handle_p):
    name = c.cstr(name_p)
    if name != "xam.xex":
        return 0xC0000225
    c.put32(handle_p, 0x58AA0000)
    return 0

def k_proc_address(c, handle, ordinal, out_p):
    check(handle == 0x58AA0000, "XexGetProcedureAddress gets xam's handle")
    c.put32(out_p, export_address(LIB_XAM, ordinal))
    return 0

def x_get_state(c, user, flags, state_p):
    if user != 0:
        return 0x48F
    p = c.pad
    c.mu.mem_write(state_p, struct.pack(">IHBBhhhh", p["packet"], p["buttons"], p["lt"], p["rt"], 0, 0, 0, 0))
    return 0

def x_title_id(c):
    return c.title_id

def x_notify(c, kind, user, areas, text_p, ctx):
    raw = b""
    while True:
        ch = c.mem(text_p + len(raw), 2)
        if ch == b"\0\0":
            break
        raw += ch
    c.toasts.append(raw.decode("utf-16-be"))
    return 0

def k_physical(c, address):
    return address & 0x1FFFFFFF

def k_alloc_physical(c, flags, size, protect, min_address, max_address, alignment):
    align = max(alignment, 4096)
    addr = (c.phys_next + align - 1) & ~(align - 1)
    if addr + size > PHYS_POOL + PHYS_POOL_SIZE:
        return 0
    c.phys_next = addr + size
    c.phys_allocs[addr] = (size, protect)
    return addr

def k_alloc_virtual(c, base_p, size_p, alloc_type, protect, debug):
    size = (c.u32(size_p) + 0xFFF) & ~0xFFF
    check(c.u32(base_p) == 0 and alloc_type & 0x1000 and protect == 4,
          "NtAllocateVirtualMemory: committed read/write memory anywhere")
    addr = c.virt_next
    if not size or addr + size > VIRT_POOL + VIRT_POOL_SIZE:
        return 0xC0000017  # STATUS_NO_MEMORY
    c.virt_next = addr + size
    c.virt_allocs[addr] = size
    c.mu.mem_write(base_p, struct.pack(">I", addr))
    c.mu.mem_write(size_p, struct.pack(">I", size))
    return 0

def k_free_virtual(c, base_p, size_p, free_type, debug):
    addr = c.u32(base_p)
    check(addr in c.virt_allocs and free_type == 0x8000, f"NtFreeVirtualMemory releases an allocation ({addr:#x})")
    c.virt_frees.append(addr)
    c.virt_allocs.pop(addr, None)
    return 0

def k_query_statistics(c, stats):
    check(c.u32(stats) == 104, "MmQueryStatistics: the 104-byte structure")
    free_pages = (PHYS_POOL + PHYS_POOL_SIZE - c.phys_next) // 4096
    c.mu.mem_write(stats + 12, struct.pack(">I", free_pages))
    return 0

def k_free_physical(c, kind, address):
    check(address in c.phys_allocs, f"MmFreePhysicalMemory of an allocated block ({address:#x})")
    c.phys_frees.append(address)
    size, _ = c.phys_allocs.pop(address, (0, 0))
    if address + size == c.phys_next:  # the latest block: its memory can be handed out again
        c.phys_next = address
    return 0

def k_set_gfx_cb(c, callback, data):
    c.gfx_cb = (callback, data)
    return 0

def k_set_base_priority(c, thread, increment):
    c.priorities.append((thread, increment))
    return 0

def k_vdswap(c, *args):
    c.vdswaps.append(args)
    return 0

def x_message_box(c, user, title, text, count, buttons_p, focus, flags, result_p, ov_p):
    buttons = [c.wstr(c.u32(buttons_p + 4 * i)) for i in range(count)]
    c.boxes.append({"user": user, "title": c.wstr(title), "text": c.wstr(text), "buttons": buttons,
                    "focus": focus, "result": result_p, "ov": ov_p})
    c.put32(ov_p, 997)  # ERROR_IO_PENDING until the test "presses" a button
    return 997

def x_raw_passthrough(c, *args):
    check(False, "real XAM raw portal API reached while the virtual portal is on")
    return 0x48F

EXPORTS = {
    (LIB_KRNL, 0x003): ("DbgPrint", 2, k_dbgprint),
    (LIB_KRNL, 0x00D): ("ExCreateThread", 7, k_create_thread),
    (LIB_KRNL, 0x019): ("ExTerminateThread", 1, k_terminate_thread),
    (LIB_KRNL, 0x05A): ("KeDelayExecutionThread", 3, k_delay),
    (LIB_KRNL, 0x084): ("KeQuerySystemTime", 1, k_query_time),
    (LIB_KRNL, 0x0CF): ("NtClose", 1, k_close),
    (LIB_KRNL, 0x0D2): ("NtCreateFile", 9, k_create_file),
    (LIB_KRNL, 0x0DB): ("NtFlushBuffersFile", 2, k_flush),
    (LIB_KRNL, 0x0E4): ("NtQueryDirectoryFile", 9, k_query_directory),
    (LIB_KRNL, 0x0E7): ("NtQueryFullAttributesFile", 2, k_query_full_attributes),
    (LIB_KRNL, 0x0E8): ("NtQueryInformationFile", 5, k_query_info),
    (LIB_KRNL, 0x0F0): ("NtReadFile", 8, k_read_file),
    (LIB_KRNL, 0x0F7): ("NtSetInformationFile", 5, k_set_info),
    (LIB_KRNL, 0x0FF): ("NtWriteFile", 8, k_write_file),
    (LIB_KRNL, 0x103): ("ObCreateSymbolicLink", 2, k_create_link),
    (LIB_KRNL, 0x104): ("ObDeleteSymbolicLink", 1, k_delete_link),
    (LIB_KRNL, 0x125): ("RtlEnterCriticalSection", 1, k_cs),
    (LIB_KRNL, 0x12B): ("RtlImageXexHeaderField", 2, k_header_field),
    (LIB_KRNL, 0x12C): ("RtlInitAnsiString", 2, k_init_ansi),
    (LIB_KRNL, 0x12E): ("RtlInitializeCriticalSection", 1, k_cs),
    (LIB_KRNL, 0x130): ("RtlLeaveCriticalSection", 1, k_cs),
    (LIB_KRNL, 0x195): ("XexGetModuleHandle", 2, k_module_handle),
    (LIB_KRNL, 0x197): ("XexGetProcedureAddress", 3, k_proc_address),
    (LIB_KRNL, 0x0BE): ("MmGetPhysicalAddress", 1, k_physical),
    (LIB_KRNL, 0x1D5): ("VdSetGraphicsInterruptCallback", 2, k_set_gfx_cb),
    (LIB_KRNL, 0x099): ("KeSetBasePriorityThread", 2, k_set_base_priority),
    (LIB_KRNL, 0x0BA): ("MmAllocatePhysicalMemoryEx", 6, k_alloc_physical),
    (LIB_KRNL, 0x0BD): ("MmFreePhysicalMemory", 2, k_free_physical),
    (LIB_KRNL, 0x0CC): ("NtAllocateVirtualMemory", 5, k_alloc_virtual),
    (LIB_KRNL, 0x0C6): ("MmQueryStatistics", 1, k_query_statistics),
    (LIB_KRNL, 0x0DC): ("NtFreeVirtualMemory", 4, k_free_virtual),
    (LIB_KRNL, 0x25B): ("VdSwap", 10, k_vdswap),
    (LIB_XAM, 0x2CA): ("XamShowMessageBoxUI", 9, x_message_box),
    (LIB_XAM, XAM_GETSTATE): ("XamInputGetState", 3, x_get_state),
    (LIB_XAM, XAM_TITLEID): ("XamGetCurrentTitleId", 0, x_title_id),
    (LIB_XAM, 0x290): ("XNotifyQueueUI", 5, x_notify),
    (LIB_XAM, XAM_GETRAW): ("XamInputNonControllerGetRaw", 3, x_raw_passthrough),
    (LIB_XAM, XAM_SETRAW): ("XamInputNonControllerSetRaw", 2, x_raw_passthrough),
}

# ------------------------------------------------------------------------------------------------
# The test
# ------------------------------------------------------------------------------------------------
def tiled_offset(x, y, pitch):
    """Byte offset of a 32 bpp pixel in a tiled surface, ported from Xenia's texture_address.h."""
    outer = ((y >> 5) * (pitch >> 5) + (x >> 5)) << 6
    inner = (((y >> 1) & 7) << 3) | (x & 7)
    oi = (outer | inner) << 2
    bank = (y >> 4) & 1
    pipe = ((x >> 3) & 3) ^ (((y >> 3) & 1) << 1)
    return ((y & 1) << 4) | (pipe << 6) | (bank << 11) | (oi & 15) | (((oi >> 4) & 1) << 5) | \
        (((oi >> 5) & 7) << 8) | ((oi >> 8) << 12)

def run_pop_menu(c, worker, LB, RB, A):
    def tap():
        c.pad["buttons"] = LB | RB
        c.run_thread(worker, 3)
        c.pad["buttons"] = 0
        c.run_thread(worker, 3)

    def press(button, status=0):
        box = c.boxes[-1]
        c.put32(box["result"], button)
        c.put32(box["ov"], status)
        c.run_thread(worker, 20)

    print("skypop: tap LB+RB opens the console's message box")
    c.pad["buttons"] = LB | RB | A  # holding another button isn't a tap
    c.run_thread(worker, 3)
    c.pad["buttons"] = 0
    c.run_thread(worker, 3)
    check(not c.boxes, "LB+RB with another button doesn't open the menu")
    tap()
    check(len(c.boxes) == 1, "a tap opens the menu")
    b = c.boxes[-1] if c.boxes else {"title": "", "text": "", "buttons": [], "focus": -1, "user": -1}
    print("   box|", b["title"], "|", b["buttons"], "| focus", b["focus"])
    for line in b["text"].splitlines():
        print("   box|  ", line)
    check(b["title"] == "SPYRO" and b["user"] == 0, "box: title is the selected figure")
    check(b["buttons"] == ["« Previous", "Take off the rift", "Next »"] and b["focus"] == 1,
          "box: Previous / Take off the rift / Next, middle focused")
    check("Figure 1 of 3" in b["text"] and "On the rift now" in b["text"], "box: position and status")
    c.run_thread(worker, 10)
    check(len(c.boxes) == 1, "no second box while one is open")

    press(2)  # Next
    check(len(c.boxes) == 2, "Next reopens the box")
    b = c.boxes[-1] if len(c.boxes) > 1 else b
    print("   box|", b["title"], "|", b["buttons"], "| focus", b["focus"])
    check(b["title"] == "TREE REX" and b["focus"] == 2, "box: next figure, Next still focused")
    check(b["buttons"][1] == "Swap in", "box: Swap in when other figures are on the portal")
    press(1)  # Swap in
    check(len(c.boxes) == 2, "an action closes the menu")
    log = bytes(c.fs.read("hdd/The Static Rift/staticrift.log") or b"").decode("latin-1")
    check("Swap: taking Spyro off" in log and "Swapping in Tree Rex: it goes on once the game has seen"
          in log and "Swapped in Tree Rex" not in log,
          "Swap in: the Skylander off first, Tree Rex not yet on (the game hasn't seen it come off)")

    def game_polls(n):
        getraw_thunk = c.game_imports[XAM_GETRAW][1]
        st, ln, buf = STACKS + 0x8100, STACKS + 0x8104, STACKS + 0x8200
        for _ in range(n):
            c.put32(st, 0xFFFF0000)
            c.put32(ln, 32)
            c.call(getraw_thunk, [st, ln, buf], STACKS + 0x10000, label="game GetRaw")

    game_polls(4)            # the game is told the figures are gone
    c.run_thread(worker, 3)
    game_polls(20)           # ...and keeps polling, but too soon for the new figure
    c.run_thread(worker, 3)
    log = bytes(c.fs.read("hdd/The Static Rift/staticrift.log") or b"").decode("latin-1")
    check("Swapped in Tree Rex" not in log, "Swap in: still waiting a moment after the game saw the removal")
    game_polls(60)
    c.run_thread(worker, 45)  # 0.9 s
    log = bytes(c.fs.read("hdd/The Static Rift/staticrift.log") or b"").decode("latin-1")
    check("Swapped in Tree Rex" in log, "log: Tree Rex swapped in once the game had seen the rift empty")
    state_txt = bytes(c.fs.read("hdd/The Static Rift/rift_state.txt") or b"").decode()
    check("Giants\\Tree Rex.bin" in state_txt and "Spyro.bin" not in state_txt, "rift_state.txt: Tree Rex only")

    print("skypop: swapping in a trap swaps only traps; the Skylander stays on")
    tap()
    check(len(c.boxes) == 3 and c.boxes[-1]["buttons"][1] == "Take off the rift", "menu reopens on Tree Rex")
    press(2)  # Next: the Air trap
    b = c.boxes[-1]
    print("   box|", b["title"], "|", b["buttons"])
    check(len(c.boxes) == 4 and b["title"] == "AIR TOUCAN" and b["buttons"][1] == "Swap in",
          "box: the trap, Swap in")
    press(1)  # Swap in
    log = bytes(c.fs.read("hdd/The Static Rift/staticrift.log") or b"").decode("latin-1")
    state_txt = bytes(c.fs.read("hdd/The Static Rift/rift_state.txt") or b"").decode()
    check("Swap: taking Tree Rex off" not in log and "Swapped in Air Toucan" in log and
          "Giants\\Tree Rex.bin" in state_txt and "Air Toucan.bin" in state_txt,
          "the trap went straight on next to Tree Rex (no trap was on, so nothing came off)")

    tap()
    check(len(c.boxes) == 5 and c.boxes[-1]["buttons"][1] == "Take off the rift", "menu reopens on the trap")
    press(0, status=1223)  # B: cancelled
    check(len(c.boxes) == 5, "B closes the menu without acting")
    c.pad["buttons"] = LB | RB | A  # held for the controller-hiding checks that follow
    c.run_thread(worker, 3)
    check(len(c.boxes) == 5, "LB+RB with A held doesn't open the menu")

def run_drawn_menu(c, worker, game_stack, vdswap_thunk, gfx_thunk, state_thunk, sym, LB, RB, A, RIGHT,
                   xex_path):
    pattern = struct.pack(">I", 0x11223344)
    c.mu.mem_write(FRONT_BUFFERS, pattern * (0x800000 // 4))
    scratch = GAME + 0xC000
    fbs = [FRONT_BUFFERS, FRONT_BUFFERS + 0x400000]
    current = sym["sp_menu_current"]

    def present(i):
        """The game presents its buffer i; returns the arguments that reached the kernel."""
        virt = fbs[i]
        fetch = struct.pack(">IIIIII", 1 << 31 | (FB_PITCH >> 5) << 22 | 2, (virt & 0xFFFFF000) | 2 << 6 | 6,
                            (FB_W - 1) | (FB_H - 1) << 13, 0, 0, 0)
        c.mu.mem_write(scratch + 0x100, fetch)
        c.mu.mem_write(scratch + 0x200, struct.pack(">IIIII", virt, 6, 0, FB_W, FB_H))
        args = [scratch, scratch + 0x100, 0x1234, 0x5678, 0xBEEF0001, scratch + 0x200, scratch + 0x204,
                scratch + 0x208]
        stack = [scratch + 0x20C, scratch + 0x210]
        c.call(vdswap_thunk, args, game_stack, label="game VdSwap", stack_args=stack)
        got = tuple(c.vdswaps[-1]) if c.vdswaps else ()
        return args + stack, got

    def game_frame_ok(label):
        want, got = present(0)
        check(got == tuple(want), f"{label}: VdSwap gets the game's own frame, all ten arguments intact")

    def game_buttons():
        sp = STACKS + 0x8300
        c.call(state_thunk, [0, 0, sp], game_stack, label="game XamInputGetState")
        return struct.unpack(">H", c.mem(sp + 4, 2))[0]

    def tap(buttons=LB | RB):
        c.pad["buttons"] = buttons
        c.run_thread(worker, 2)
        c.pad["buttons"] = 0
        c.run_thread(worker, 2)

    def run_draw_until(cond, what, frames=40):
        for _ in range(frames):
            if cond():
                return True
            c.run_thread(draw, 2)
            present(0)
        return check(cond(), what)

    def surfaces():
        return sorted(a for a, (size, prot) in c.phys_allocs.items() if prot & 0x400)

    def save_png(virt, name):
        try:
            from PIL import Image
        except ImportError:
            return
        img = Image.new("RGB", (FB_W, FB_H))
        raw = bytes(c.mem(virt, FB_PITCH * 736 * 4))
        px = img.load()
        for y in range(FB_H):
            for x in range(FB_W):
                v = struct.unpack_from(">I", raw, tiled_offset(x, y, FB_PITCH))[0]
                px[x, y] = ((v >> 16) & 255, (v >> 8) & 255, v & 255)
        out = xex_path.rsplit("/", 1)[0] + "/" + name
        img.save(out)
        print("   fb| saved", out)

    scanout = lambda: c.u32(DISPLAY_ADDRESS_REG)
    game_phys = fbs[0] & 0x1FFFFFFF
    c.put32(DISPLAY_ADDRESS_REG, game_phys)  # the kernel shows the game's frame

    # Direct3D's interrupt handler: it puts its newest frame (a variable here) on screen as a late
    # frame (flip at the next line: D1GRPH_FLIP_CONTROL = 1), and notes its arguments.
    d3d_cb, d3d_vars = GAME + 0xC800, GAME + 0xC900
    code = [0x3D608200, 0x616BC900,              # lis r11, 0x8200; ori r11, r11, 0xC900
            0x818B0000, 0x906B0004, 0x908B0008,  # lwz r12, 0(r11); stw r3, 4(r11); stw r4, 8(r11)
            0x3D407FC8, 0x614A6110,              # lis r10, 0x7FC8; ori r10, r10, 0x6110
            0x39200001, 0x912A0038,              # li r9, 1; stw r9, 0x38(r10)   (flip control)
            0x918A0000, 0x4E800020]              # stw r12, 0(r10); blr
    c.mu.mem_write(d3d_cb, struct.pack(f">{len(code)}I", *code))
    c.put32(d3d_vars, game_phys)
    c.call(gfx_thunk, [d3d_cb, 0xD3D0C0DE], game_stack, label="game VdSetGraphicsInterruptCallback")
    check(c.gfx_cb is not None and c.gfx_cb[0] == c.sym["sp_gfx_irq_hook"] and c.gfx_cb[1] == 0xD3D0C0DE,
          "the kernel gets the overlay's interrupt handler, with Direct3D's context")

    def vblank(next_frame):
        """A vertical blank: Direct3D's handler (through the overlay's) shows `next_frame`."""
        c.put32(d3d_vars, next_frame & 0x1FFFFFFF)
        c.call(c.gfx_cb[0], [0, c.gfx_cb[1]], game_stack, label="graphics interrupt")
        check(c.u32(d3d_vars + 4) == 0 and c.u32(d3d_vars + 8) == 0xD3D0C0DE,
              "Direct3D's handler runs with the kernel's arguments")

    vblank(fbs[1])
    check(scanout() == fbs[1] & 0x1FFFFFFF, "menu closed: the interrupt leaves Direct3D's frame on screen")

    print("skydrawn: the game presents frames through its (hooked) VdSwap")
    game_frame_ok("menu closed")
    present(1)
    present(0)
    draw = c.threads[1] if len(c.threads) > 1 else None
    pin = c.threads[2] if len(c.threads) > 2 else None
    check(draw is not None and pin is not None, "drawing and scan-out threads started")
    if pin is not None and c.system_dll:
        check(not pin["flags"] & 2 and not draw["flags"] & 2,
              "Dashlaunch build: the drawing and scan-out threads are game threads, started from the "
              "game's first VdSwap")
    if draw is None or pin is None:
        return
    c.run_thread(draw, 5)
    c.run_thread(pin, 3)
    check(scanout() == fbs[1] & 0x1FFFFFFF, "closed: the scan-out register is left alone")
    check(c.u32(current) == 0 and not surfaces(), "closed: no menu frame, nothing allocated")
    check(game_buttons() == 0, "game sees the idle pad")

    print("skydrawn: a tap of LB+RB opens the menu (no holding)")
    tap()
    check(game_buttons() == 0, "menu open: the game's controller is idle")
    c.pad["buttons"] = A | RIGHT  # buttons pressed in the menu never reach the game
    check(game_buttons() == 0, "menu open: A and Right are hidden from the game")
    c.pad["buttons"] = 0
    run_draw_until(lambda: c.u32(current) != 0, "the menu frame is published")
    frames = surfaces()
    check(len(frames) == 2, f"two menu frames allocated (got {len(frames)})")
    frame_ptr = c.u32(current)
    fetch1, fb_word = c.u32(frame_ptr + 4), c.u32(frame_ptr + 24)
    shown = fb_word
    check(shown in frames and (fetch1 & 0xFFFFF000) == shown and (fetch1 & 0xFFF) == (2 << 6 | 6),
          "menu frame: fetch constant and front buffer word point at a menu surface, same format bits")
    want, got = present(1)
    check(len(got) == 10 and got[1] == frame_ptr and got[5] == frame_ptr + 24 and
          got[:1] + got[2:5] + got[6:] == tuple(want[:1] + want[2:5] + want[6:]),
          "menu open: VdSwap is handed the menu frame (r4, r8) with the other arguments untouched")
    check(c.mem(fbs[0], 64) == pattern * 16 and c.mem(fbs[1] + 0x200000, 64) == pattern * 16,
          "the game's own buffers are never written")
    c.run_thread(pin, 2)
    check(scanout() == shown & 0x1FFFFFFF, f"menu open: scan-out points at the menu surface ({scanout():#x})")
    vblank(fbs[0])
    check(scanout() == shown & 0x1FFFFFFF,
          "menu open: right after Direct3D's handler moves scan-out to its frame, it's back on the menu")
    check(c.u32(FLIP_CONTROL_REG) == 0,
          "menu open: Direct3D's flip-at-the-next-line is turned off again in the same interrupt")
    check(c.u32(c.sym["sp_lock_uses"]) > 0 and c.u32(UPDATE_REG) == 0,
          "menu open: the surface registers were locked around Direct3D's handler and unlocked after")
    check(c.virt_allocs, "menu pictures and buffers in virtual memory")
    check(c.u32(c.sym["sp_pin_game"]) == fbs[0] & 0x1FFFFFFF, "the game's frame is remembered")
    c.put32(DISPLAY_ADDRESS_REG, (fbs[1] & 0x1FFFFFFF))  # moved back some other way
    c.run_thread(pin, 1)
    check(scanout() == shown & 0x1FFFFFFF, "the scan-out thread undoes it too")
    check(any(inc == 15 for _, inc in c.priorities), "the scan-out thread runs time-critical")
    centre = struct.unpack(">I", c.mem(shown + tiled_offset(FB_W // 2, 300, FB_PITCH), 4))[0]
    check(centre != 0 and centre >> 24 == 0xFF, f"menu surface drawn as opaque A8R8G8B8 ({centre:#010x})")
    save_png(shown, "emu_drawn.png")

    print("skydrawn: Right (without LB+RB) moves to Tree Rex in the other frame; A adds it")
    tap(RIGHT)
    run_draw_until(lambda: c.u32(current) not in (0, frame_ptr), "the next menu frame is published")
    frame2 = c.u32(current)
    shown2 = c.u32(frame2 + 24)
    check(shown2 in frames and shown2 != shown, "the change is drawn into the other surface, then shown")
    c.run_thread(pin, 2)
    check(scanout() == shown2 & 0x1FFFFFFF, "scan-out follows to the other surface")
    vblank(fbs[0])
    check(scanout() == shown2 & 0x1FFFFFFF and c.u32(c.sym["sp_pin_game"]) == fbs[0] & 0x1FFFFFFF,
          "interrupt: back on the new menu surface; a menu surface is never taken for the game's frame")
    save_png(shown2, "emu_drawn2.png")
    tap(A)
    c.run_thread(worker, 2)
    log = bytes(c.fs.read("hdd/The Static Rift/staticrift.log") or b"").decode("latin-1")
    check("Placed 'Giants\\Tree Rex.bin'" in log, "A puts Tree Rex on the portal")
    check(not any("Tree Rex" in t for t in c.toasts), "no toasts with the menu up")

    print("skydrawn: another tap of LB+RB closes it")
    c.pad["buttons"] = LB | RB
    c.run_thread(worker, 2)
    check(game_buttons() == 0, "closing combo still hidden while held")
    c.pad["buttons"] = 0
    c.run_thread(worker, 3)
    check(game_buttons() == 0, "pad idle after closing")
    c.pad["buttons"] = A
    check(game_buttons() == A, "the game has the controller back")
    c.pad["buttons"] = 0
    run_draw_until(lambda: c.u32(current) == 0, "menu frame withdrawn")
    game_frame_ok("menu closed again")
    for _ in range(6):
        c.run_thread(draw, 2)
        present(0)
    check(len(surfaces()) == 2, "menu memory kept until the scan-out thread has handed the display back")
    c.run_thread(pin, 2)
    check(scanout() == fbs[0] & 0x1FFFFFFF, f"closed: scan-out back on the game's last frame ({scanout():#x})")
    check(c.u32(FLIP_CONTROL_REG) == 1, "closed: the game's flip control setting is put back")
    vblank(fbs[1])
    check(scanout() == fbs[1] & 0x1FFFFFFF, "closed: the interrupt leaves Direct3D's frames alone again")
    run_draw_until(lambda: not surfaces(), "menu memory freed a few frames after closing")
    check(len(c.phys_frees) >= 2, "menu frames freed")
    check(not c.virt_allocs, "menu pictures and buffers freed too")
    log = bytes(c.fs.read("hdd/The Static Rift/staticrift.log") or b"").decode("latin-1")
    check("Menu opened" in log and "Menu closed" in log, "log: open / close")

    def present_start(i, sleeps):
        """The game presents its buffer i on a stack of its own, and may be held there (paused)."""
        virt = fbs[i]
        fetch = struct.pack(">IIIIII", 1 << 31 | (FB_PITCH >> 5) << 22 | 2, (virt & 0xFFFFF000) | 2 << 6 | 6,
                            (FB_W - 1) | (FB_H - 1) << 13, 0, 0, 0)
        c.mu.mem_write(scratch + 0x300, fetch)
        c.mu.mem_write(scratch + 0x400, struct.pack(">IIIII", virt, 6, 0, FB_W, FB_H))
        args = [scratch, scratch + 0x300, 0x1234, 0x5678, 0xBEEF0001, scratch + 0x400, scratch + 0x404,
                scratch + 0x408]
        return c.call_start(vdswap_thunk, args, STACKS + 0xA0000, sleeps,
                            stack_args=[scratch + 0x40C, scratch + 0x410])

    saved_next = c.phys_next
    print("skydrawn: no memory for a menu frame and the game not presenting: the pop-up menu")
    c.phys_next = PHYS_POOL + PHYS_POOL_SIZE - 0x100000  # 1 MB left: no room for a 3.7 MB frame
    boxes_before = len(c.boxes)
    tap()
    c.run_thread(draw, 1500)  # waits up to 2 s for the game to pause, then gives up
    c.run_thread(worker, 4)
    check(len(c.boxes) > boxes_before, "the pop-up menu opens instead of the full-screen one")
    if len(c.boxes) > boxes_before:
        check(c.boxes[-1]["buttons"][0] == "« Previous", "pop-up menu: the figure browser")
        c.put32(c.boxes[-1]["ov"], 1223)  # closed with B
        c.run_thread(worker, 5)
    check(not surfaces(), "nothing left allocated by the attempt")
    log = bytes(c.fs.read("hdd/The Static Rift/staticrift.log") or b"").decode("latin-1")
    check("not enough memory for the menu (1280x720): a menu frame" in log and "KB free" in log,
          "log: what ran out and how much memory was free")
    check("opened the pop-up menu" in log, "log: the pop-up menu was used instead")

    print("skydrawn: no memory for a menu frame: the game is paused and its own frames used")
    present(1)
    present(0)  # the game's buffers: 0 is the newest, 1 the one before
    swaps_before = len(c.vdswaps)
    tap()
    c.run_thread(draw, 3)  # asks for the pause and waits
    game = present_start(1, 1)  # the game's next frame: held in VdSwap
    check(not game["done"], "the game's render thread waits inside its VdSwap call")
    run_draw = lambda: c.run_thread(draw, 2)
    for _ in range(60):
        if c.u32(sym["sp_pin_phys"]):
            break
        run_draw()
    pinned = c.u32(sym["sp_pin_phys"])
    check(pinned in (fbs[0] & 0x1FFFFFFF, fbs[1] & 0x1FFFFFFF),
          f"menu drawn into one of the game's own front buffers ({pinned:#x})")
    check(not surfaces(), "no menu frame memory of its own")
    check(bytes(c.mem(fbs[1], 64)) != struct.pack(">I", 0x11223344) * 16,
          "the menu's pixels are in the game's buffer")
    check(len(c.vdswaps) == swaps_before, "no frames presented while paused")
    c.pad["buttons"] = RIGHT
    c.run_thread(worker, 2)
    c.pad["buttons"] = 0
    c.run_thread(worker, 2)
    for _ in range(80):
        run_draw()
        if c.u32(sym["sp_pin_phys"]) not in (0, pinned):
            break
    check(c.u32(sym["sp_pin_phys"]) in (fbs[0] & 0x1FFFFFFF, fbs[1] & 0x1FFFFFFF) and
          c.u32(sym["sp_pin_phys"]) != pinned, "the next figure goes into the game's other buffer")
    tap()
    for _ in range(10):
        run_draw()
    game = c.call_resume(game, 5)
    check(game["done"], "closed: the game carries on (its VdSwap returns)")
    check(len(c.vdswaps) == swaps_before + 1 and c.vdswaps[-1][1] == scratch + 0x300,
          "closed: the held frame is presented with the game's own arguments")
    for _ in range(10):
        run_draw()
        present(0)
    check(c.u32(sym["sp_pin_phys"]) == 0 and not c.virt_allocs, "closed: nothing pinned, nothing allocated")
    log = bytes(c.fs.read("hdd/The Static Rift/staticrift.log") or b"").decode("latin-1")
    check("game paused, drawing into its own front buffers" in log, "log: the game's frames were used")

    print("skydrawn: room for one menu frame only (and the game not pausing): one frame")
    c.phys_next = PHYS_POOL + PHYS_POOL_SIZE - 0x600000  # 6 MB left: one frame and the margin
    tap()
    c.run_thread(draw, 1500)  # the pause request times out
    run_draw_until(lambda: c.u32(current) != 0, "one-frame menu presented")
    check(len(surfaces()) == 1, f"one menu frame allocated (got {len(surfaces())})")
    c.pad["buttons"] = RIGHT
    c.run_thread(worker, 2)
    c.pad["buttons"] = 0
    c.run_thread(worker, 2)
    for _ in range(10):
        c.run_thread(draw, 2)
        present(0)
    tap()
    run_draw_until(lambda: not surfaces(), "one-frame menu freed after closing")
    check(not c.virt_allocs, "its working memory freed too")
    c.phys_next = saved_next

    # For the controller-hiding checks that follow: the combo is held but the worker doesn't
    # run, so the menu doesn't open again.
    c.pad["buttons"] = LB | RB | A


def figure(seed):
    return bytes((i * 7 + seed) & 0xFF for i in range(1024))

def main():
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    variant = sys.argv[3] if len(sys.argv) == 4 else "classic"
    if variant not in ("classic", "pop", "drawn", "rgh"):
        sys.exit(__doc__)
    print(f"variant: {variant}")
    xex = parse_xex(open(sys.argv[1], "rb").read())
    sym = elf_symbols(sys.argv[2])
    print(f"XEX: base {xex['base']:#x}, entry {xex['entry']:#x}, flags {xex['flags']:#x}, "
          f"{sum(len(l['records']) for l in xex['libs'])} import records")
    rgh = variant == "rgh"
    drawn = variant in ("drawn", "rgh")
    if rgh:
        check(xex["flags"] == 0x8, "module flags are DLL only (a system DLL, as Dashlaunch loads)")
    else:
        check(xex["flags"] == 0x9, "module flags are TITLE | DLL")
    check(xex["entry"] == sym["_xrt_dll_entry"], "entry point is the guest-entry trampoline")

    c = Console(xex, sym)
    c.fs.write("hdd/The Static Rift/staticrift.ini", (
        "[General]\r\nEnabled = 1\r\nNotifications = 1\r\nSaveWrites = 1\r\nRememberPortal = 1\r\n"
        "[Controls]\r\nModifier = LB+RB\r\nHideComboFromGame = 1\r\n"
        "[Startup]\r\nFigure1 = Spyro\r\n").encode())
    spyro, tree_rex = figure(3), figure(9)
    c.fs.write("hdd/The Static Rift/Figures/Spyro.bin", spyro)
    c.fs.write("hdd/The Static Rift/Figures/Giants/Tree Rex.bin", tree_rex)
    c.fs.write("hdd/The Static Rift/Figures/readme.txt", b"x" * 1024)  # ignored extension
    c.fs.write("hdd/The Static Rift/Figures/short.bin", b"x" * 1000)   # wrong size
    # Four folders deep, like the pack's traps (2.0 fixed these being skipped).
    # (character id 212 in the dump: an Air trap, for the swap-by-kind checks)
    c.fs.write("hdd/The Static Rift/Figures/Zz Extra/7) Traps/1) Crystal Traps/Air/Air Toucan.bin",
               figure(210)[:0x10] + (212).to_bytes(2, "little") + figure(210)[0x12:])
    if drawn:
        # The menu's index and, when tools/mkassets.py output is at $SKYPORTAL_ASSETS, its pictures.
        import os
        c.fs.write("hdd/The Static Rift/UI/figures.idx", (
            "# test index\n"
            "spyro\tSpyro\t1\t5\tSeries 1\t1. Spyro's Adventure\\Spyro.qoi\t1\n"
            "giants/treerex\tTree Rex\t2\t7\tGiant\t2. Giants\\Tree Rex.qoi\n"
            "6imaginators/kingpen\tKing Pen\t6\t4\tSensei\t-\t6\n").encode())
        # A figure from a later game than the one running (Trap Team): left out of the menu.
        c.fs.write("hdd/The Static Rift/Figures/6. Imaginators/King Pen.bin", figure(21))
        assets = os.environ.get("SKYPORTAL_ASSETS", "")
        if assets and os.path.isdir(os.path.join(assets, "UI")):
            for rel in ["UI/background.qoi"] + [f"UI/elements/{e}.qoi" for e in ("magic", "life")] + \
                       ["UI/games/1.qoi", "UI/games/2.qoi", "Images/1. Spyro's Adventure/Spyro.qoi",
                        "Images/2. Giants/Tree Rex.qoi"]:
                path = os.path.join(assets, rel)
                if os.path.exists(path):
                    c.fs.write("hdd/The Static Rift/" + rel, open(path, "rb").read())
            print("assets: real UI pictures from", assets)

    game_stack = STACKS + 0x10000
    getraw_var, getraw_thunk = c.game_imports[XAM_GETRAW]
    setraw_var, setraw_thunk = c.game_imports[XAM_SETRAW]
    state_var, state_thunk = c.game_imports[XAM_GETSTATE]
    title_var, _ = c.game_imports[XAM_TITLEID]

    if rgh:
        c.fs.write("hdd/The Static Rift/staticrift.log", b"previous session's log\r\n")
        c.fs.write("hdd/StaticRift_trace.txt", b"previous session's trace\r\n")
        print("boot: the dashboard runs; Dashlaunch loads the plugin")
        c.title_id = 0xFFFE07D1
        c.put32(XEXEC_HANDLE_VAR, 0)
        c.system_dll = True
    print("DllMain(DLL_PROCESS_ATTACH)")
    ok = c.call(xex["entry"], [0xD11, 1, 0], game_stack, label="DllMain attach")
    check(ok == 1, "DllMain returns TRUE")
    if rgh:
        check(c.u32(getraw_var) == export_address(LIB_XAM, XAM_GETRAW), "no game yet: nothing patched")
        check(len(c.threads) == 1 and c.threads[0]["flags"] & 2, "one worker thread, a system thread")
        worker = c.threads[0]
        print("worker thread: boot startup on the dashboard")
        c.run_thread(worker, 5)
        log = bytes(c.fs.read("hdd/The Static Rift/staticrift.log") or b"").decode("latin-1")
        for line in log.splitlines():
            print("   log|", line)
        check("(The Static Rift, Dashlaunch plugin, drawn menu (skydrawn))" in log, "log: Dashlaunch banner")
        check(c.fs.read("hdd/The Static Rift/staticrift-previous.log") == b"previous session's log\r\n" and
              c.fs.read("hdd/StaticRift_trace-previous.txt") == b"previous session's trace\r\n",
              "the last session's log and trace are kept (-previous) when new ones start")
        trace = bytes(c.fs.read("hdd/StaticRift_trace.txt") or b"").decode("latin-1")
        for line in trace.splitlines():
            print("   trace|", line)
        check("Dashlaunch plugin attach" in trace and "running program FFFE07D1" in trace and
              "worker thread created" in trace and "worker thread running" in trace and
              "data folder SKYHDD:\\The Static Rift" in trace and "startup done" in trace,
              "trace file (Hdd1 root) records each boot step")
        check("Figure index loaded" in log, "log: UI\\figures.idx loaded at boot")
        check("figure file(s) in Figures" not in log, "no scan before a game starts")
        check(c.u32(getraw_var) == export_address(LIB_XAM, XAM_GETRAW), "dashboard: nothing patched")
        check(len(c.threads) == 1 and c.threads[0]["flags"] & 2,
              "boot: only the worker, a system thread (the menu's threads start with each game)")
        print("Trap Team starts")
        c.title_id = 0x41560911
        c.put32(XEXEC_HANDLE_VAR, GAME_LDR)
        c.run_thread(worker, 3)
    check(c.u32(getraw_var) == sym["sp_hook_get_raw_guest"], "game's GetRaw import points at the hook")
    check(c.u32(setraw_var) == sym["sp_hook_set_raw_guest"], "game's SetRaw import points at the hook")
    check(c.u32(state_var) == sym["sp_hook_get_state_guest"], "game's GetState import points at the hook")
    check(c.u32(title_var) == export_address(LIB_XAM, XAM_TITLEID), "unrelated import untouched")
    proc_var, proc_thunk = c.krnl_imports[0x197]
    check(c.u32(proc_var) == sym["sp_hook_get_proc_guest"], "game's XexGetProcedureAddress import points at the hook")
    vdswap_var, vdswap_thunk = c.krnl_imports[0x25B]
    gfx_var, gfx_thunk = c.krnl_imports[0x1D5]
    if drawn:
        check(c.u32(vdswap_var) == sym["sp_vdswap_hook"], "game's VdSwap import points at the overlay stub")
        check(c.u32(gfx_var) == sym["sp_set_gfx_cb_hook"],
              "game's VdSetGraphicsInterruptCallback import points at the overlay stub")
    else:
        check(c.u32(vdswap_var) == export_address(LIB_KRNL, 0x25B), "game's VdSwap import untouched")
        check(c.u32(gfx_var) == export_address(LIB_KRNL, 0x1D5), "game's VdSetGraphicsInterruptCallback import untouched")
    words = struct.unpack(">IIII", c.mem(getraw_thunk, 16))
    target = ((words[0] & 0xFFFF) << 16) + struct.unpack(">h", struct.pack(">H", words[1] & 0xFFFF))[0]
    check((target & 0xFFFFFFFF) == sym["sp_hook_get_raw_guest"] and words[2:] == (0x7D6903A6, 0x4E800420),
          "game's GetRaw thunk jumps to the hook")
    if not rgh:
        check(len(c.threads) == 1, "one worker thread created")
    for reason in (2, 3):
        c.call(xex["entry"], [0xD11, reason, 0], game_stack, label=f"DllMain reason {reason}")

    if not rgh:
        print("worker thread: startup")
        worker = c.threads[0]
        c.run_thread(worker, 3)
    log = bytes(c.fs.read("hdd/The Static Rift/staticrift.log") or b"").decode("latin-1")
    for line in log.splitlines():
        print("   log|", line)
    trace = bytes(c.fs.read("hdd/StaticRift_trace.txt") or b"").decode("latin-1")
    for line in trace.splitlines():
        print("   trace|", line)
    patched = 6 if drawn else 4
    if rgh:
        check(f"---- Trap Team (title 41560911): hooked {patched} import(s) while it loads ----" in log,
              f"log: Trap Team hooked ({patched} imports) as it starts")
    else:
        check("DllMain attach" in trace and f"patched {patched} import(s)" in trace and "worker thread created" in trace
              and "startup done" in trace, "trace file records each startup step")
        check('Game: TitleID = 0x41560911  MediaID = 0x12345678  ModuleVersion = "0.0.5.1"' in log,
              "log: execution ID in ini syntax")
    if drawn:
        check("4 figure file(s) in Figures: 3 listed in Trap Team, 1 for later games" in log,
              "log: three figures listed for Trap Team (one four folders deep), the Imaginators one left out")
    else:
        check("3 figure file(s) in Figures: 3 listed in Trap Team, 0 for later games" in log,
              "log: three valid figures found, one four folders deep (bad ones ignored)")
    check("folder(s) in Figures are empty or couldn't be opened" not in log, "log: every folder opened")
    if drawn:
        check("Figure index loaded" in log, "log: UI\\figures.idx loaded")
    if not rgh:
        check(f"Hooked {patched} import(s) before the game started" in log, f"log: {patched} imports hooked from DllMain")
        style = {"classic": "notifications", "pop": "pop-up menu (skypop)", "drawn": "drawn menu (skydrawn)"}[variant]
        check(f"(The Static Rift, Peer Pressure sideload, {style})" in log, "log: menu style in the banner")
    check("XAM function(s), ordinals (hex): 1CF 191 4A1 4A2" in log, "log: game's XAM imports named")
    check("Game imports XexGetProcedureAddress: yes (hooked)" in log, "log: lookup import hooked")
    check("Placed 'Spyro.bin' in slot 0" in log, "log: startup figure placed")
    check(any("The Static Rift is open - 1 on the rift, 3 figures" in t for t in c.toasts),
          "toast: portal ready")
    check(set(c.links) >= {"SKYHDD:", "SKYUSB0:", "SKYUSB1:", "SKYUSB2:"}, "drive links made")

    def game_set(payload):
        buf = STACKS + 0x8000
        frame = bytes([0x0B, 0x14]) + bytes(payload) + bytes(30 - len(payload))
        c.mu.mem_write(buf, frame)
        return c.call(setraw_thunk, [32, buf], game_stack, label="game SetRaw")

    def game_get():
        st, ln, buf = STACKS + 0x8100, STACKS + 0x8104, STACKS + 0x8200
        c.put32(st, 0xFFFF0000)
        c.put32(ln, 32)
        res = c.call(getraw_thunk, [st, ln, buf], game_stack, label="game GetRaw")
        state = c.u32(st) >> 16
        n = c.u32(ln)
        return res, state, c.mem(buf, n)

    def game_poll(cmd):
        for _ in range(12):
            res, state, data = game_get()
            check(res == 0, "GetRaw succeeds")
            if state == 1 and data[2:3] == bytes([cmd]):
                return data
        return None

    print("game: looks up XamInputNonControllerGetRawEx at run time and polls through it")
    outp = STACKS + 0x8400
    res = c.call(proc_thunk, [0x58AA0000, 0x52A, outp], game_stack, label="game XexGetProcedureAddress")
    check(res == 0 and c.u32(outp) == sym["sp_hook_get_raw_ex_guest"], "looked-up GetRawEx is the hook")
    res = c.call(proc_thunk, [0x58AA0000, 0x1CF, outp], game_stack, label="game XexGetProcedureAddress")
    check(res == 0 and c.u32(outp) == export_address(LIB_XAM, 0x1CF), "other lookups untouched")
    st, ln, buf = STACKS + 0x8100, STACKS + 0x8104, STACKS + 0x8200
    c.put32(st, 0xFFFF0000); c.put32(ln, 32)
    res = c.call(sym["sp_hook_get_raw_ex_guest"], [5, buf, ln, st], game_stack, label="game GetRawEx (looked up)")
    check(res == 0, "GetRawEx through the looked-up pointer succeeds")

    print("game: portal handshake through its own import stubs")
    res, state, data = game_get()
    check(res == 0 and state == 0 and data == b"", "inactive portal: reads say 'no new data'")
    check(game_set(b"R") == 0, "SetRaw 'R' succeeds")
    r = game_poll(ord("R"))
    check(r is not None and r[:5] == bytes([0x0B, 0x14, 0x52, 0x02, 0x1B]), "ready reply 52 02 1B")
    game_set(b"A\x01")
    check(game_poll(ord("A")) is not None, "activate reply")
    s = game_poll(ord("S"))
    check(s is not None and (s[3] & 3) == 3 and (s[8] & 1), "status: Spyro 'added' in slot 0, portal active")
    game_set(bytes([ord("Q"), 0x20, 0]))
    q = game_poll(ord("Q"))
    check(q is not None and q[3] == 0x10 and q[5:21] == spyro[:16], "block 0 of slot 0 is Spyro's")

    print("game: writes a block; worker saves it")
    new_block = bytes(range(0xA0, 0xB0))
    game_set(bytes([ord("W"), 0x00, 0x08]) + new_block)
    w = game_poll(ord("W"))
    check(w is not None and w[3] == 0x10, "write acknowledged")
    c.run_thread(worker, 20)
    saved = bytes(c.fs.read("hdd/The Static Rift/Figures/Spyro.bin"))
    check(saved[0x80:0x90] == new_block and saved[:0x80] == spyro[:0x80], "Spyro.bin has the game's write")
    backup = c.fs.read("hdd/The Static Rift/Backups/Spyro.bin.bak")
    check(backup is not None and bytes(backup) == spyro, "original kept in Backups/Spyro.bin.bak")

    LB, RB, A, RIGHT = 0x0100, 0x0200, 0x1000, 0x0008
    if variant == "classic":
        print("controller: LB+RB, Right, A")
        c.pad["buttons"] = LB | RB
        c.run_thread(worker, 30)
        c.pad["buttons"] = LB | RB | RIGHT
        c.run_thread(worker, 2)
        c.pad["buttons"] = LB | RB
        c.run_thread(worker, 30)
        c.pad["buttons"] = LB | RB | A
        c.run_thread(worker, 5)
        for t in c.toasts:
            print("   toast|", t)
        check(any(t.startswith("Spyro [on the rift]") for t in c.toasts), "toast: current selection")
        check(any(t.startswith("Tree Rex  (2/3)") for t in c.toasts), "toast: moved to Tree Rex")
        check(any(t == "+ Tree Rex placed on the rift" for t in c.toasts), "toast: Tree Rex placed")
        state_txt = bytes(c.fs.read("hdd/The Static Rift/rift_state.txt") or b"").decode()
        check("Spyro.bin" in state_txt and "Giants\\Tree Rex.bin" in state_txt, "rift_state.txt remembers both")
    elif variant == "pop":
        run_pop_menu(c, worker, LB, RB, A)
    else:  # drawn, rgh
        run_drawn_menu(c, worker, game_stack, vdswap_thunk, gfx_thunk, state_thunk, sym, LB, RB, A, RIGHT,
                       sys.argv[1])

    print("game: reads the pad while the combo is held (buttons hidden), then the new figure")
    sp = STACKS + 0x8300
    res = c.call(state_thunk, [0, 0, sp], game_stack, label="game XamInputGetState")
    buttons = struct.unpack(">H", c.mem(sp + 4, 2))[0]
    check(res == 0 and buttons == LB | RB, f"menu buttons hidden from the game (got {buttons:#06x})")
    c.pad["buttons"] = A
    res = c.call(state_thunk, [0, 0, sp], game_stack, label="game XamInputGetState")
    check(struct.unpack(">H", c.mem(sp + 4, 2))[0] == A, "A reaches the game without the combo")
    # skypop swapped Tree Rex in after the game saw slot 0 emptied, so it reuses slot 0; the
    # other menus added it next to Spyro.
    slot = 0 if variant == "pop" else 1
    s = game_poll(ord("S"))
    check(s is not None and ((s[3] >> (2 * slot)) & 3) in (1, 3), f"status: slot {slot} holds Tree Rex")
    game_set(bytes([ord("Q"), 0x20 + slot, 3]))
    q = game_poll(ord("Q"))
    check(q is not None and q[3] == 0x10 + slot and q[5:21] == tree_rex[48:64],
          f"block 3 of slot {slot} is Tree Rex's")

    c.run_thread(worker, 400)  # 8 s of worker time: the 5-second portal-use report
    log = bytes(c.fs.read("hdd/The Static Rift/staticrift.log") or b"").decode("latin-1")
    for line in log.splitlines():
        if "portal use" in line or "lookups" in line:
            print("   log|", line)
    check("Game run-time export lookups (2 calls, 1 redirected to the portal): xam:52A xam:1CF" in log,
          "log: the game's run-time lookups and the redirect")
    check("Game portal use:" in log, "log: periodic portal-use report")

    if rgh:
        print("quit to the dashboard")
        c.title_id = 0xFFFE07D1
        c.put32(XEXEC_HANDLE_VAR, 0)
        for _ in range(60):  # the game's threads are gone; the worker gives them half a second
            c.run_thread(worker, 2)
            log = bytes(c.fs.read("hdd/The Static Rift/staticrift.log") or b"").decode("latin-1")
            if "Title 41560911 ended" in log:
                break
        check("Title 41560911 ended" in log, "log: the game's exit noticed")
        state_txt = bytes(c.fs.read("hdd/The Static Rift/rift_state.txt") or b"").decode()
        check("Giants\\Tree Rex.bin" in state_txt, "rift_state.txt remembers the rift at exit")
        print("Trap Team starts again (a fresh copy of the game, imports unpatched)")
        c.build_game()
        toasts_before = len(c.toasts)
        c.title_id = 0x41560911
        c.put32(XEXEC_HANDLE_VAR, GAME_LDR)
        c.run_thread(worker, 3)
        log = bytes(c.fs.read("hdd/The Static Rift/staticrift.log") or b"").decode("latin-1")
        tail = log[log.rfind("Title 41560911 ended"):]
        for line in tail.splitlines():
            print("   log|", line)
        getraw_var, getraw_thunk = c.game_imports[XAM_GETRAW]
        setraw_var, setraw_thunk = c.game_imports[XAM_SETRAW]
        vdswap_var, _ = c.krnl_imports[0x25B]
        check(c.u32(getraw_var) == sym["sp_hook_get_raw_guest"], "relaunch: GetRaw hooked again")
        check(c.u32(vdswap_var) == sym["sp_vdswap_hook"], "relaunch: VdSwap hooked again")
        check(log.count("hooked 6 import(s) while it loads") == 2, "log: second launch hooked")
        check("2 figure(s) on the rift at start" in tail,
              "relaunch: the figures stay on the rift between games (as on a real portal)")
        check(any("The Static Rift is open" in t for t in c.toasts[toasts_before:]), "relaunch: ready toast")
        res, st, data = game_get()
        check(res == 0 and st == 0 and data == b"", "relaunch: a fresh portal session (inactive until the game starts it)")
        check(game_set(b"R") == 0, "relaunch: SetRaw 'R'")
        check(game_poll(ord("R")) is not None, "relaunch: ready reply")
        game_set(b"A\x01")
        check(game_poll(ord("A")) is not None, "relaunch: activate reply")
        s = game_poll(ord("S"))
        check(s is not None and s[3] & 0x0F and (s[8] & 1),
              "relaunch: the new game is told about the figures on the rift")
        check(len(c.threads) == 3, "no new threads before the new game presents a frame")

        draw = c.threads[1]
        vdswap_thunk = c.krnl_imports[0x25B][1]
        current = sym["sp_menu_current"]

        def present2(i):
            virt = FRONT_BUFFERS + 0x400000 * i
            scratch = GAME + 0xC000
            fetch = struct.pack(">IIIIII", 1 << 31 | (FB_PITCH >> 5) << 22 | 2,
                                (virt & 0xFFFFF000) | 2 << 6 | 6, (FB_W - 1) | (FB_H - 1) << 13, 0, 0, 0)
            c.mu.mem_write(scratch + 0x100, fetch)
            c.mu.mem_write(scratch + 0x200, struct.pack(">IIIII", virt, 6, 0, FB_W, FB_H))
            c.call(vdswap_thunk, [scratch, scratch + 0x100, 0x1234, 0x5678, 0xBEEF0001, scratch + 0x200,
                                  scratch + 0x204, scratch + 0x208], game_stack, label="game VdSwap",
                   stack_args=[scratch + 0x20C, scratch + 0x210])

        def open_menu(what):
            present2(1)
            present2(0)
            c.pad["buttons"] = LB | RB
            c.run_thread(worker, 2)
            c.pad["buttons"] = 0
            c.run_thread(worker, 2)
            for _ in range(60):
                if c.u32(current):
                    break
                c.run_thread(draw, 2)
                present2(0)
            return check(c.u32(current) != 0, what)

        print("relaunched game: menu opened, then the game quits to the dashboard with it open")
        present2(0)
        check(len(c.threads) == 5 and not c.threads[3]["flags"] & 2 and not c.threads[4]["flags"] & 2,
              "relaunch: the new game's first frame starts its own drawing and scan-out threads")
        draw, pin = c.threads[3], c.threads[4]
        c.run_thread(draw, 2)
        c.run_thread(pin, 2)
        open_menu("relaunch: the menu opens in the new game")
        c.run_thread(pin, 2)
        frees_before = (len(c.phys_frees), len(c.virt_frees))
        c.title_id = 0xFFFE07D1
        c.put32(XEXEC_HANDLE_VAR, 0)
        for _ in range(80):  # the game, and the menu's threads with it, are gone
            c.run_thread(worker, 2)
            log = bytes(c.fs.read("hdd/The Static Rift/staticrift.log") or b"").decode("latin-1")
            if log.count("Title 41560911 ended") == 2:
                break
        check("The game exited with the menu open" in log, "log: the exit with the menu up noticed")
        check(log.count("Title 41560911 ended") == 2, "the exit finished")
        check(c.u32(current) == 0 and c.u32(sym["sp_pin_phys"]) == 0, "the menu is off the display")
        check((len(c.phys_frees), len(c.virt_frees)) == frees_before,
              "nothing freed after the game is gone (the menu's memory is let go of, never freed twice)")
        print("the game starts a third time: the menu works again")
        c.build_game()
        c.title_id = 0x41560911
        c.put32(XEXEC_HANDLE_VAR, GAME_LDR)
        c.run_thread(worker, 3)
        vdswap_thunk = c.krnl_imports[0x25B][1]
        c.call(c.krnl_imports[0x1D5][1], [GAME + 0xC800, 0xD3D0C0DE], game_stack,
               label="game VdSetGraphicsInterruptCallback")
        present2(0)
        check(len(c.threads) == 7, "third launch: its own drawing and scan-out threads")
        draw, pin = c.threads[5], c.threads[6]
        c.run_thread(draw, 2)
        c.run_thread(pin, 2)
        open_menu("third launch: the menu opens again")
        c.pad["buttons"] = LB | RB
        c.run_thread(worker, 2)
        c.pad["buttons"] = 0
        c.run_thread(worker, 2)
        for _ in range(60):
            if not c.u32(current):
                break
            c.run_thread(draw, 2)
            c.run_thread(pin, 2)
            present2(0)
        check(c.u32(current) == 0, "third launch: the menu closes")

        print("[Menu] FullScreenMenu = 0: the next game uses the console's pop-up menu")
        ini = bytes(c.fs.read("hdd/The Static Rift/staticrift.ini")).decode()
        c.fs.write("hdd/The Static Rift/staticrift.ini", (ini + "[Menu]\r\nFullScreenMenu = 0\r\n").encode())
        c.title_id = 0xFFFE07D1
        c.put32(XEXEC_HANDLE_VAR, 0)
        for _ in range(80):
            c.run_thread(worker, 2)
            log = bytes(c.fs.read("hdd/The Static Rift/staticrift.log") or b"").decode("latin-1")
            if log.count("Title 41560911 ended") == 3:
                break
        check(log.count("Title 41560911 ended") == 3, "third game's exit finished")
        c.build_game()
        c.title_id = 0x41560911
        c.put32(XEXEC_HANDLE_VAR, GAME_LDR)
        c.run_thread(worker, 3)
        boxes = len(c.boxes)
        allocs = len(c.phys_allocs)
        c.pad["buttons"] = LB | RB
        c.run_thread(worker, 2)
        c.pad["buttons"] = 0
        c.run_thread(worker, 4)
        log = bytes(c.fs.read("hdd/The Static Rift/staticrift.log") or b"").decode("latin-1")
        check("Menu: pop-up menu ([Menu] FullScreenMenu = 0)" in log and len(c.boxes) > boxes,
              "FullScreenMenu = 0: LB+RB opens the console's pop-up menu")
        check(len(c.phys_allocs) == allocs and c.u32(current) == 0, "... and the full-screen menu stays closed")

    print("DllMain(DLL_PROCESS_DETACH)")
    check(c.call(xex["entry"], [0xD11, 0, 0], game_stack, label="DllMain detach") == 1, "detach returns TRUE")

    print(f"kernel/XAM calls: {sum(c.calls.values())} across {len(c.calls)} exports")
    for v in c.violations[:10]:
        print("   convention violation:", v)
    check(not c.violations, "every kernel/XAM call followed the console's convention")
    if failures:
        print(f"{len(failures)} check(s) failed")
        return 1
    print("all emulated end-to-end checks passed")
    return 0

if __name__ == "__main__":
    sys.exit(main())
