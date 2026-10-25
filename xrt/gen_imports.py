#!/usr/bin/env python3
"""Generate imports.S for the open (no-XDK) build from imports.txt.

For each kernel/XAM function import this emits three things:

1. The import slot (type-0 record) and the 16-byte thunk (type-1 and type-2 records, then
   `mtctr r11; bctr`), plus an entry in `.xsdk.imports` that elf2xex.py turns into the XEX import
   libraries header. The loader rewrites the thunk's first two words to load the export's address
   into r11. This record layout is openpak/x360-sdk's (tools/gen_imports.py, MIT), which titles
   built that way load with on retail consoles.

2. A wrapper, under the C name, that the plugin's (stock 32-bit SysV) code calls. It calls the
   export the way the console's own MSVC code does (measured in xsdk docs/HARDWARE.md):
     - every argument zero-extended to 64 bits in r3-r10 (MSVC callers do; callees may rely on it),
     - a 9th argument in its 8-byte slot at 0x50(r1), the value in the low word (0x54),
     - its own 0x60-byte frame, so a callee that spills registers into its caller's argument
       slots (variadic ones do, up to 0x57) writes into the wrapper's frame, not the plugin's.
   r14-r31 come back whole from MSVC code, so nothing else is needed on the way back.

For a variable import only the slot is emitted, under the export's name: the loader writes the
variable's address into it.
"""

import sys
from pathlib import Path

LIBRARY_NAME_BYTES = 16
RECORD_VARIABLE = 0x00000000
RECORD_THUNK = 0x01000000
RECORD_THUNK_SECOND = 0x02000000
FRAME = 0x60  # 0x10 header + 8 argument slots + the 9th slot, 16-byte aligned


def parse(path):
    imports = []
    for number, line in enumerate(path.read_text().splitlines(), 1):
        fields = line.split("#", 1)[0].split()
        if not fields:
            continue
        if len(fields) not in (4, 5):
            sys.exit(f"{path}:{number}: expected library, ordinal, name, argument count or 'variable'[, c-name]")
        library, ordinal, name, count = fields[:4]
        if len(library) >= LIBRARY_NAME_BYTES:
            sys.exit(f"{path}:{number}: library name too long")
        entry = {"library": library, "ordinal": int(ordinal, 16), "name": name}
        if count == "variable":
            entry["variable"] = True
        else:
            entry["variable"] = False
            entry["args"] = int(count)
            if entry["args"] > 9:
                sys.exit(f"{path}:{number}: more than 9 arguments isn't supported")
            entry["cname"] = fields[4] if len(fields) == 5 else name
        imports.append(entry)
    return imports


def library_name(library):
    return '\t.ascii\t"' + library + "\\0" * (LIBRARY_NAME_BYTES - len(library)) + '"'


def record(slot, thunk, entry):
    return [
        "\t.section .xsdk.imports, \"\"",
        f"\t.long\t{slot}",
        f"\t.long\t{thunk}",
        f"\t.long\t{entry['ordinal']:#x}",
        "\t.long\t0",
        library_name(entry["library"]),
    ]


def variable(entry):
    name = entry["name"]
    return [
        f"// {entry['library']} {entry['ordinal']:#05x} {name} (variable): the loader writes its address here.",
        f"\t.section .rodata.xrt_imp_{name}, \"a\"",
        "\t.balign 4",
        f"\t.globl\t{name}",
        f"\t.type\t{name}, @object",
        f"{name}:",
        f"\t.long\t{RECORD_VARIABLE | entry['ordinal']:#010x}",
        f"\t.size\t{name}, 4",
        *record(name, 0, entry),
        "",
    ]


def function(entry):
    name, cname, args, ordinal = entry["name"], entry["cname"], entry["args"], entry["ordinal"]
    slot, thunk = f"xrt_imp_{name}", f"xrt_thunk_{name}"
    lines = [
        f"// {entry['library']} {ordinal:#05x} {name}, {args} argument(s); C calls {cname}.",
        f"\t.section .rodata.{slot}, \"a\"",
        "\t.balign 4",
        f"\t.globl\t{slot}",  # the loader-bound export address, readable from C
        f"{slot}:",
        f"\t.long\t{RECORD_VARIABLE | ordinal:#010x}",
        "",
        f"\t.section .text.xrt_thunk_{name}, \"ax\"",
        "\t.balign 16",
        f"{thunk}:",
        f"\t.long\t{RECORD_THUNK | ordinal:#010x}",
        f"\t.long\t{RECORD_THUNK_SECOND | ordinal:#010x}",
        "\tmtctr\t11",
        "\tbctr",
        "",
        f"\t.section .text.xrt_wrap_{name}, \"ax\"",
        "\t.balign 4",
        f"\t.globl\t{cname}",
        f"\t.type\t{cname}, @function",
        f"{cname}:",
        "\tmflr\t0",
        "\tstw\t0, 4(1)",
        f"\tstwu\t1, -{FRAME:#x}(1)",
    ]
    if args >= 9:
        # SysV put the 9th argument at 8(caller's r1) = FRAME + 8 now; MSVC reads slot 9 at 0x50.
        lines += [
            f"\tlwz\t11, {FRAME + 8:#x}(1)",
            "\tli\t12, 0",
            "\tstw\t12, 0x50(1)",
            "\tstw\t11, 0x54(1)",
        ]
    for reg in range(3, 3 + min(args, 8)):
        lines.append(f"\tclrldi\t{reg}, {reg}, 32")
    lines += [
        f"\tbl\t{thunk}",
        f"\taddi\t1, 1, {FRAME:#x}",
        "\tlwz\t0, 4(1)",
        "\tmtlr\t0",
        "\tblr",
        f"\t.size\t{cname}, . - {cname}",
        "",
        *record(slot, thunk, entry),
        "",
    ]
    return lines


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: gen_imports.py imports.txt imports.S")
    imports = parse(Path(sys.argv[1]))
    out = ["// Generated by xrt/gen_imports.py from xrt/imports.txt. Do not edit.", ""]
    for entry in imports:
        out += variable(entry) if entry["variable"] else function(entry)
    Path(sys.argv[2]).write_text("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
