# Third-party notices

## TheBiemGamer/skylanders-portal (MIT)

`src/core/portal_core.cpp` re-implements, in C++03 for the Xbox 360 XDK, the portal command
handling, status-report layout and Xbox 360 frame bridge of
https://github.com/TheBiemGamer/skylanders-portal (files `src/portal/software/software_portal.cpp`,
`src/portal/xam_bridge.cpp`, `src/portal/xbox_frame.cpp`).

    MIT License

    Copyright (c) 2026 TheBiemGamer

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.

## openpak/x360-sdk "xsdk" (MIT)

The open (no-XDK) toolchain in `xrt/` builds on xsdk (https://github.com/openpak/x360-sdk):

- `xrt/elf2xex.py` is xsdk's `tools/elf2xex.py`, changed to take the image base as a parameter
  and to write title DLLs (`--dll`).
- `xrt/gen_imports.py` emits xsdk's import record and thunk layout (`tools/gen_imports.py`), and
  `xrt/dll.ld` follows `src/xsdk.ld`.
- `xrt/xrt.cpp`, `xrt/entry.S` and the import wrappers follow the kernel usage and the
  calling-convention measurements recorded in xsdk's `docs/HARDWARE.md`. No code from there was
  copied.

    MIT License

    Copyright (c) 2026 Mena

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.

## Xenia Canary (BSD)

No code is copied. The XAM raw non-controller API signatures used in `src/xbox/hooks.cpp`
were taken from Xenia Canary's `src/xenia/kernel/xam/xam_input.cc`. The kernel/XAM ordinals in
`xrt/imports.txt` were checked against its `xboxkrnl_table.inc` and `xam_table.inc`. The loader
behaviour that `tests/emu/run_dll.py` imitates (DLL entry calls, thunk rewriting) comes from its
`kernel_state.cc` and `xex_module.cc`.

## xkelib

Required for the XDK build (not included). The open build in `xrt/` doesn't use it. Provides the kernel/XAM export declarations and import
libraries used by `src/xbox`.

## DejaVu Sans Bold (skydrawn menu font)

`src/xbox/overlay_font.h` holds glyph bitmaps rasterized by `tools/mkfont.py` from DejaVu Sans Bold.
DejaVu fonts are based on Bitstream Vera; changes are in the public domain. Bitstream Vera license
(summary): the fonts may be used, copied, modified, embedded and distributed freely, provided the
copyright notice is kept, modified fonts are renamed, and the fonts are not sold by themselves.
Copyright (c) 2003 Bitstream, Inc. All Rights Reserved. Bitstream Vera is a trademark of
Bitstream, Inc.

## Xenia (tiled surface addressing, register and VdSwap facts)

The tiled-address formula in `src/xbox/overlay.cpp` and `tests/emu/run_dll.py` is ported from
Xenia's `src/xenia/gpu/texture_address.h` (BSD license, (c) Xenia Developers).

## QOI image format (skydrawn pictures)

`src/xbox/qoi.cpp` (decoder) and the encoder in `tools/mkassets.py` implement the "Quite OK Image
Format" from its specification (https://qoiformat.org, Dominic Szablewski, MIT/CC0). No code from
the reference implementation was copied.

## Dolphin Emulator (figure elements, used by tools/mkassets.py only)

`tools/mkassets.py` reads Dolphin's Skylander list
(`Source/Core/Core/IOS/USB/Emulated/Skylanders/Skylander.cpp`, GPL-2.0-or-later, Dolphin
Emulator Project) at build time to look up each character's element. Only names and elements end
up in the generated `UI\figures.idx`; no Dolphin code is included in the plugin or the script.

## Pictures, logos and backgrounds

The release package's figure pictures come from SkylandersNFC's Skylanders Image Generator
(card art by Mr Shadow, with cards and coin art by SkylandersNFC, Dim, IndominusRex, Sobersu,
Cha0s and fruitsnack where he has no card of a figure) and the Skylanders Ultimate NFC Pack
(premade creation pictures). The game logos, element icons and portal background come from the
project owner's image collection. All are converted to QOI by `tools/mkassets.py` and aren't
in this source tree. See [CREDITS.md](CREDITS.md). Skylanders and its artwork are trademarks
and property of Activision.

## Skylanders Ultimate NFC Pack (figure dumps in the release package)

The release package's `Figures` folder is the Skylanders Ultimate NFC Pack V15 by SkylandersNFC
and its contributors (https://github.com/skylandersNFC/Skylanders-Ultimate-NFC-Pack), with
some long names shortened for the console's file system. See CREDITS.md for the contributors.

## Linux radeon driver (display register names)

The AVIVO display register offsets and bits used by the menu (D1GRPH_PRIMARY_SURFACE_ADDRESS,
D1GRPH_UPDATE / UPDATE_LOCK, D1GRPH_FLIP_CONTROL) were looked up in the Linux kernel's
`drivers/gpu/drm/radeon/r500_reg.h` (GPL-2.0). No code is copied.

## Peer Pressure, RB3Enhanced, Runes

Peer Pressure (grimdoomer) loads the plugin; its sideload `.ini` format follows RB3Enhanced's
example. Texthead1's Runes documents the Skylander figure format. Neither is included here.
