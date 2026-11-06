# Project Kaos-Byte4.1.0v: The Static Rift

A virtual **Portal of Power** for the six Xbox 360 Skylanders games, for consoles with
**Peer Pressure**, or **BadUpdate + XeUnshackle / RGH / JTAG** (Dashlaunch). No portal hardware: your figures are files on the hard drive, and you put them
on and take them off with a full-screen menu (tap **LB + RB**) while you play. Progress the game
saves goes back into the figure files.

- Works in Spyro's Adventure, Giants, Swap Force, Trap Team, SuperChargers and Imaginators
- Full-screen menu with card art for every figure, its element, game and series
- Three lists on R3: every figure the running game can use, your own rosters (My Picks), and
  each game's own figures
- Every figure type in the Skylanders Ultimate NFC Pack V15: Skylanders, traps and villains,
  vehicles, creation crystals, magic items, chests and unreleased figures
- Pauses the game while the menu is open, in every game
- **The phone remote** (Peer Pressure since 3.0.0v; RGH / JTAG / BadUpdate since 4.0.0v). The Xbox serves a page to your phone on the
  same Wi-Fi: see what's on the Rift, search, browse the same lists, and put figures on or take them
  off from the phone while you play, with live updates over one connection that mends itself and
  **Quick tap** (one tap swaps a figure in, for speedruns). Since 4.1.0v a figure goes on as soon
  as the game will take it
- **Nicknames, Favorites and your own lists** (4.0.0v): give any figure a nickname from the menu
  (START, with an on-screen keyboard) or the phone; star favorites and make lists of your own on
  the phone (a roster, a speedrun, anything). They show on both, the lists in the menu's R3 cycle

**Download:** click **Releases** on the right of this page, open the newest release and download
from **Assets**: `Kaos-Byte4.1.0v-Release.zip` for **Peer Pressure**, or
`Kaos-Byte4.1.0v-RGH-Release.zip` for **RGH / JTAG / BadUpdate**. (Not "Source code" or the green
Code button: that's the code for building it, not the plugin.)

**Install:** each guide walks you through it, FileZilla included.
- **Peer Pressure:** see **[INSTALL.md](INSTALL.md)**. Unzip the release and drag its two
  folders onto `Hdd1` with FileZilla.
- **BadUpdate / RGH / JTAG (Dashlaunch):** see **[INSTALL-RGH.md](INSTALL-RGH.md)**. Drag
  two folders over, add one line to `launch.ini`, restart. Tested on BadUpdate + XeUnshackle;
  RGH and JTAG run the same Dashlaunch and should work the same way.

**Credits:** see **[CREDITS.md](CREDITS.md)**. This project builds on many people's work.

## Building

The plugin builds with clang + ld.lld + Python, no Xbox 360 SDK needed:

    make -C xrt VARIANT=drawn        # -> xrt/build/drawn/KaosByte.xex (Peer Pressure)
    make -C xrt VARIANT=rgh          # -> xrt/build/rgh/KaosByteDSL.xex (Dashlaunch)
    make -C tests run                # unit tests
    make -C tests emu UNICORN_PY=... UNICORN_LIB=...   # runs the plugin on an emulated CPU
    make -C tests emu-remote FOLDER=<The Static Rift> ...  # the phone remote, with a real browser
    make -C tests emu-remote-rgh FOLDER=<The Static Rift> ...  # the same on the RGH build

The figure index, pictures and background are made by `tools/mkassets.py` (see its header), and
the phone remote's small pictures (`The Static Rift/Phone`) by `tools/mkphone.py`.
Developer notes from earlier versions are in [docs/TECHNICAL.md](docs/TECHNICAL.md).

## License

MIT (see [LICENSE](LICENSE)); third-party parts keep their own licenses
([THIRD_PARTY.md](THIRD_PARTY.md)). Skylanders is a trademark of Activision; this is an
unofficial fan project.
