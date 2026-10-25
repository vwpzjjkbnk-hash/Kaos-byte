> Developer notes from the project's earlier versions (when it was called SkyPortal360 / skydrawn). Folder and file names here use the old names: `SkyPortal` is now `The Static Rift`, `skyportal.ini` is now `staticrift.ini` and `skydrawn16.xex` is now `Kaos-Byte2.0v.xex`.

# Project Kaos-Byte2.0v — The Static Rift

(Formerly SkyPortal360.) The menu, the data folder (`The Static Rift`), `staticrift.ini` and the
plugin (`Kaos-Byte2.0v.xex`) carry the new name; a folder still called `SkyPortal` and a
`skyportal.ini` keep working.

A virtual **Portal of Power** for the Xbox 360 Skylanders games on a modded console. No portal
hardware: figures are 1 KB dump files on the hard drive or a USB stick, and you put them on /
take them off the portal with a controller combo while you play. Progress the game saves to a
figure is written back into its file.

It comes in two builds from the same source:

| Your console | Build | Loaded by |
|---|---|---|
| RGH / JTAG, or BadUpdate + XeUnshackle (Dashlaunch) | `Release` → `SkyPortal360.xex` | Dashlaunch `launch.ini` plugin |
| **Peer Pressure** softmod | `Release_PeerPressure` → `SkyPortal360.xex` | Peer Pressure DLL sideloading |

Peer Pressure doesn't load Dashlaunch plugins. It loads per-game title DLLs instead, which
suits SkyPortal: it only ever patches the game's own import table, never the kernel or XAM.

Supported titles (built in):

| Game | Title ID |
|---|---|
| Skylanders: Spyro's Adventure | 415608AB |
| Skylanders: Giants | 415608DA |
| Skylanders: Swap Force | 415608E9 |
| Skylanders: Trap Team | 41560911 |
| Skylanders: SuperChargers | 4156091F |
| Skylanders: Imaginators | 41560930 |

## How it works

The Xbox 360 games don't talk to the portal over plain USB HID like the PS3/Wii/PC versions do.
The 360 portal has a Microsoft security chip, and the games reach it only through XAM's raw
"non-controller" input API:

| XAM export | Ordinal | Use |
|---|---|---|
| `XamInputNonControllerGetRaw` | 0x4A1 | read a 32-byte portal frame |
| `XamInputNonControllerSetRaw` | 0x4A2 | send a 32-byte command frame |
| `XamInputNonControllerGetRawEx` / `SetRawEx` | 0x52A / 0x52B | same, newer dashboards |

When a Skylanders title starts, SkyPortal360 rewrites that title's imports of those four
functions so they land in a software portal instead of XAM. The Dashlaunch build does this from
a system thread that watches for Skylanders titles; the Peer Pressure build is loaded into the
game itself and does it before the game's first instruction runs.
The security chip never comes into it, because XAM (which does the chip handshake with real
hardware) is bypassed entirely. The software portal speaks the standard portal protocol
(`R`, `A`, `S`, `Q`, `W`, `C`, `J`, `M` ... wrapped in the 360's `0B 14` frame) and serves
figures out of RAM.

```
 Skylanders title ──XamInputNonController*Raw──▶ [patched import] ──▶ SkyPortal360
                                                                        ├─ XamBridge (0B 14 frames)
                                                                        ├─ VirtualPortal (16 slots)
                                                                        └─ FigureLibrary  ⇄ Hdd:\SkyPortal\Figures
```

## Install

Both builds share the same data folder. Copy `dist\SkyPortal` to the root of the console's
hard drive, so you have:

```
Hdd:\SkyPortal\skyportal.ini
Hdd:\SkyPortal\Figures\...           <- your figure dumps
```

(A `SkyPortal` folder on a USB stick works too; the hard drive is checked first.) Unplug any
real portal; the virtual one replaces it.

### Peer Pressure

1. `SkyPortal360.xex` is **already built** and sits in `dist\peerpressure\SideLoading\`. It
   was built without the Xbox 360 XDK by the open toolchain in `xrt/` (see *Building without the
   XDK*). You can also build it yourself, with the XDK (**Release_PeerPressure**) or without it
   (`make -C xrt`).
2. Copy everything in `dist\peerpressure\SideLoading` (the DLL and the six `.ini` files) into
   `Hdd1:\PeerPressure\SideLoading\`:
   ```
   Hdd1:\PeerPressure\SideLoading\SkyPortal360.xex
   Hdd1:\PeerPressure\SideLoading\415608ab.ini    Spyro's Adventure
   Hdd1:\PeerPressure\SideLoading\415608da.ini    Giants
   Hdd1:\PeerPressure\SideLoading\415608e9.ini    Swap Force
   Hdd1:\PeerPressure\SideLoading\41560911.ini    Trap Team
   Hdd1:\PeerPressure\SideLoading\4156091f.ini    SuperChargers
   Hdd1:\PeerPressure\SideLoading\41560930.ini    Imaginators
   ```
3. Start a Skylanders game. Peer Pressure shows its "dll loaded" toast, then SkyPortal shows
   `SkyPortal360: virtual portal ready`.

(`Hdd1:` is the same drive Dashlaunch-era guides call `Hdd:`. The data folder above goes at
`Hdd1:\SkyPortal\`.) Don't add anything to `launch.ini`: Peer Pressure doesn't run Dashlaunch.

The DLL loads after the game xex is in memory and before its entry point runs. It patches the
game's portal and controller imports right there, before any game code runs, then starts a
thread for the figure menu and saving, and unloads when you quit. Figure saves happen within a
quarter second of the game writing, because the DLL disappears the moment the game exits.

**If Peer Pressure shows no "dll loaded" toast:**

- Each `.ini` sets only `TitleID` and `FileName`, so it isn't tied to one disc, region or title
  update. If your Peer Pressure version wants more, open the example it installs at
  `Hdd1:\PeerPressure\SideLoading\SideLoad.ini` and add `MediaID` / `ModuleVersion` for your
  copy. Aurora's game details or `xextool -l default.xex` on a PC show them. Once SkyPortal has
  loaded once, `skyportal.log` also prints them in `.ini` syntax:
  `Game: TitleID = 0x41560911  MediaID = 0x...  ModuleVersion = "..."`.
- Alternatively, put the files next to the game instead. Peer Pressure checks the game's own
  folder first: copy `SkyPortal360.xex` and that game's `.ini` (renamed `sideload.ini`) into the
  folder that holds the game's `default.xex` (or its GOD package folder).
- If the toast appears but the game won't start, the DLL's base address clashes with the game.
  Change `baseaddr` in `xex_sideload.xml` (e.g. `0x8C000000`) and rebuild.

### Dashlaunch (RGH / JTAG / XeUnshackle)

1. Build the **Release** configuration → `bin\Release\SkyPortal360.xex`, and put it in
   `Hdd:\SkyPortal\`.
2. Add it to Dashlaunch's `launch.ini`, in `[Plugins]`, using a free slot
   (`dist\dashlaunch\launch.ini.snippet`):
   ```
   plugin1 = Hdd:\SkyPortal\SkyPortal360.xex
   ```
3. Reboot.

## Figures

Each figure is a raw **1024-byte** MIFARE 1K dump of the toy, encrypted exactly as it is on the
toy (the game decrypts it itself). Any extension works (`.bin`, `.dump`, `.sky`, ...); only the
1024-byte size is checked. Sub-folders are fine, two levels deep. Dump your own toys with a
Proxmark, ACR122U, Flipper Zero, or a real portal + SkyReader on a PC.

The file name is what the on-screen menu shows. Trap Team traps, SuperChargers vehicles and
Imaginators crystals are figures like any other.

## Controls

Hold the **modifier** (default **LB + RB**), then:

| Button | Action |
|---|---|
| D-pad Right / Left | next / previous figure (hold to scroll) |
| D-pad Down / Up | jump 10 forward / back |
| A | put the selected figure on the portal |
| B | take the selected figure off |
| Y | **swap**: take everything off, put the selected figure on |
| X | take everything off |
| Start | show what's on the portal |

A toast shows the selected figure (`Tree Rex [on portal] (12/48)`). While the modifier is held
those buttons are hidden from the game, so browsing doesn't also jump or attack. Change the
modifier in `skyportal.ini` (`Modifier = BACK+LT`, `LS+RS`, ...) if a game needs LB + RB.

The portal holds up to 16 figures at once (what the protocol allows). New files dropped into
`Figures` over FTP show up the next time you press the modifier.

## Saving and backups

When the game writes to a figure (level, gold, hats, nickname), SkyPortal360 saves the file
within a second. The first time a file changes, the untouched original is copied to
`SkyPortal\Backups\<name>.bak`, and existing backups are never overwritten. To roll back, copy a
`.bak` over the figure. Set `SaveWrites = 0` to keep figures read-only.

## Settings (`skyportal.ini`)

| Key | Default | |
|---|---|---|
| `[General] Enabled` | 1 | 0 = virtual portal off, the game talks to a real portal as normal |
| `Notifications` | 1 | toast pop-ups |
| `SaveWrites` | 1 | write game progress back into figure files |
| `RememberPortal` | 1 | keep the same figures on the portal across reboots |
| `Speaker` | 1 | report a Traptanium speaker (audio is discarded) |
| `[Controls] Modifier` | `LB+RB` | menu modifier |
| `HideComboFromGame` | 1 | hide menu buttons from the game while the modifier is held |
| `[Titles] ExtraN` | | Dashlaunch build: extra title IDs (hex) to treat as Skylanders |
| `PatchAllTitles` | 0 | Dashlaunch build: patch any title (don't: Disney Infinity uses the same API) |
| `[Startup] FigureN` | | figures placed at boot (name or path inside `Figures`) |

With Peer Pressure, which games get SkyPortal is decided by the `.ini` files in
`SideLoading`, so the `[Titles]` section isn't used.

A log is written to `SkyPortal\skyportal.log` every boot (Dashlaunch) or every game launch
(Peer Pressure). It shows which title was hooked, how
many imports were patched, every place/remove, and every save.

## Building

You need what any Dashlaunch plugin needs:

- Visual Studio 2010 with the **Xbox 360 SDK** (XDK 2.0.20871 – 2.0.21256)
- **xkelib** (kernel / XAM export headers + import libs), built once for your SDK version

Put xkelib in `.\xkelib` (or `msbuild /p:XkelibDir=C:\path\to\xkelib`), open
`SkyPortal360.sln`, and build:

- **Release | Xbox 360** → `bin\Release\SkyPortal360.xex` (Dashlaunch). If another plugin
  already uses base address `0x91C80000`, change `baseaddr` in `xex.xml`.
- **Release_PeerPressure | Xbox 360** → `bin\Release_PeerPressure\SkyPortal360.xex`
  (Peer Pressure). Same code with `SKYPORTAL_SIDELOAD` defined, built as a title DLL
  (`xex_sideload.xml`, no `<sysdll/>`).

### Building without the XDK (Peer Pressure DLL)

`xrt/` builds the Peer Pressure DLL with nothing from Microsoft: clang and ld.lld (LLVM 14 or
newer, with the PowerPC target) and Python 3. Works on Linux and WSL:

```
make -C xrt                     # -> xrt/build/SkyPortal360.xex
make -C xrt BASE=0x8C000000     # if the default base address clashes with a game
```

How it fits together:

- **`xrt/include/`**: stand-ins for `xtl.h`, `xkelib.h` and `ppcintrinsics.h`, declaring the small
  Win32 subset the plugin uses. `xrt.cpp` implements that subset directly on kernel and XAM
  exports (`NtCreateFile`, `NtQueryDirectoryFile`, `ExCreateThread`, `XamInputGetState`, and so
  on). `xrt_libc.cpp` and `xrt_printf.cpp` supply the C library pieces.
- **The calling-convention boundary.** clang emits stock 32-bit SysV code, but the console's
  code is MSVC's. The differences that matter were measured on retail consoles by the xsdk
  project, and two kinds of glue handle them:
  - `gen_imports.py` generates a wrapper per import. It zero-extends the arguments, puts a 9th
    argument in the 0x54(r1) slot, and calls inside its own 0x60-byte frame, because MSVC
    callees may spill into their caller's argument slots.
  - `entry.S` wraps everything the console calls *into* the plugin: the DLL entry point, the
    thread start, and the five game hooks. Those wrappers keep r14-r31 whole as 64-bit
    registers, because stock 32-bit code only preserves their low halves.
- **Packaging.** `dll.ld` lays the image out on 64 KB pages at `0x8A000000`. `elf2xex.py`
  (adapted from xsdk, MIT) writes an unencrypted XEX2 with module flags TITLE | DLL, the import
  table, and page hashes. Peer Pressure removes signature checks, so it loads unsigned DLLs.

The portal logic in `src/core` has no Xbox dependencies. To run its tests on a PC:

```
make -C tests run
```

The portal tests play the game's side of the handshake (ready, activate, status stream, block
reads and writes, removal, 16 slots, speaker frames, session reset, short buffers, queue
overflow) through the same `XamBridge` the hooks use. `test_patch` runs the import patcher
against a fake title import table (several import modules, duplicate imports, jump encoding,
idempotence) and checks the execution-ID reader. `test_config` checks the ini reader. It also
syntax-checks every plugin source for both builds, for 32-bit big-endian PowerPC, against stub
XDK headers (needs clang). `test_xrt` checks the open toolchain's printf and string functions
against the host C library.

`make -C tests emu` runs the built DLL itself on an emulated PowerPC 970 (Unicorn) in the
console's mode: 32-bit addressing with 64-bit registers. A fake loader, kernel, XAM,
filesystem and Skylanders game stand in for the console, and the run covers the whole flow:

- the XEX is parsed and its imports resolved the way the loader does;
- DllMain patches the game's imports;
- the worker thread reads the settings, scans the figures, writes the log and shows the toasts;
- the game does the portal handshake through its own patched import stubs, reads figure
  blocks, and writes a block that gets saved with a backup;
- the controller menu browses and places a figure, and the game sees the combo buttons hidden.

Every kernel/XAM call is checked against the console's convention, and each fake export
scribbles over its caller's argument slots, the stack below r1, and all volatile registers, as
an MSVC callee may. Every game→hook call must give back r14-r31 whole. Breaking the DLL on
purpose makes the run fail each way: a trampoline saving a register's low half only, wrappers
without zero-extension, wrappers without their frame. It needs Unicorn's Python binding
(`UNICORN_PY=... UNICORN_LIB=...`). Unicorn 2's PowerPC 64 CPU model selection has an indexing
bug: the run needs its 970 model, which takes a one-line fix in
`qemu/target/ppc/translate_init.inc.c` that adds the 32-bit model count to the requested index.

This is still an emulator, not a console. The DLL hasn't been run on real hardware yet.

## Troubleshooting

- **No "virtual portal ready" toast.** Check `skyportal.log`.
  - Dashlaunch: if it shows `Title now 4156xxxx` but never `Hooked N import(s)`, the title ID
    or XAM version differs. Add the ID under `[Titles]` and send the log lines that list the
    XAM exports.
  - Peer Pressure: if there's no log at all, the DLL wasn't loaded (no Peer Pressure toast
    either). Check the `.ini` name matches the title ID and see *If Peer Pressure shows no
    "dll loaded" toast* above. If the log says `Hooked 0 import(s) before the game started`,
    send the log: that game version reaches the portal some other way.
- **"Plug in the Portal of Power" stays up.** Unplug the real portal, make sure
  `Enabled = 1`, and check the log.
- **Figure not detected.** It must be exactly 1024 bytes. Try taking it off (B) and putting it
  back (A). Swap (Y) also forces a clean re-announce.
- **Dashlaunch plugin doesn't load.** Look for a base-address clash with another plugin, or a
  wrong path in `launch.ini`.

## Credits

- Portal protocol and Xbox 360 framing were ported from
  [TheBiemGamer/skylanders-portal](https://github.com/TheBiemGamer/skylanders-portal) (MIT),
  which was itself built from the portal emulation in Dolphin, RPCS3 and Cemu.
- [Xenia Canary](https://github.com/xenia-canary/xenia-canary) documented the XAM raw
  non-controller API (argument order, device ids 5/6, 0x20-byte buffers).
- Import patching follows the long-standing xkelib/Dashlaunch plugin approach.
- The Peer Pressure build follows
  [Peer Pressure's DLL sideloading rules](https://github.com/grimdoomer/Xbox360PeerPressure/wiki/Softmod-Features#dll-sideloading),
  with the `.ini` format taken from RB3Enhanced's sideload config.

Skylanders is a trademark of Activision. This project is unofficial, ships no game data or
figure dumps, and is for use with games and figures you own.

## Figure menus (1.4)

Three builds of the same plugin (`make -C xrt all-variants`):

| Build | File | Menu |
|---|---|---|
| classic | `SkyPortal360.xex` | hold LB+RB, toasts show the selection |
| skypop | `skypop14.xex` | tap LB+RB: the console's message box with « Previous / Put on portal / Next » |
| skydrawn | `skydrawn16.xex` | tap LB+RB: a full-screen menu (figure picture, element, game logo, series, portal state); tap again to close |

skydrawn hooks the game's `VdSwap` import (`xrt/entry.S`, `sp_vdswap_hook`). While the menu is
closed the stub only notes the game's front buffer. While it's open the stub hands VdSwap a copy of
the game's fetch constant and front-buffer address that point at one of the plugin's own frames
(`src/xbox/overlay.cpp`), so the display shows the menu and nothing is ever drawn into the game's
buffers: no flicker. Two frames are used (one shown, one being drawn); they and the pictures are
allocated on open and freed a few frames after closing. The game keeps running underneath and
sees an idle controller until the menu is closed and every button is let go.

The menu reads its pictures from the SkyPortal folder, made on the PC by `tools/mkassets.py`:
`UI\background.qoi`, `UI\elements\*.qoi`, `UI\games\1-6.qoi`, `Images\...\*.qoi` (QOI images)
and `UI\figures.idx` (name, game, element, series and picture of every dump in the Skylanders NFC
pack layout). The same script copies the pack's dumps into `Figures\` with console-safe names
(FATX: up to 42 characters, no commas). Dumps kept loose at the top of `Figures\` (your own
figures) are listed first as "My Figures" and get their details from the index by file name.

`make -C tests preview ASSETS=<SkyPortal folder>` renders the menu on the PC with the same code;
`make -C tests emu` runs all three builds on the emulated CPU (set `SKYPORTAL_ASSETS` for real
pictures), including captures of the menu frames.
