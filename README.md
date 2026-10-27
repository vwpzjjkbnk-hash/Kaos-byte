# Project Kaos-Byte2.0v: The Static Rift

A virtual **Portal of Power** for the six Xbox 360 Skylanders games, for consoles with
**Peer Pressure**, or **BadUpdate + XeUnshackle / RGH / JTAG** (Dashlaunch). No portal hardware: your figures are files on the hard drive, and you put them
on and take them off with a full-screen menu (tap **LB + RB**) while you play. Progress the game
saves goes back into the figure files.

- Works in Spyro's Adventure, Giants, Swap Force, Trap Team, SuperChargers and Imaginators
- Full-screen menu with card art for every figure, its element, game and series
- Lists only the figures the running game can use, and each Skylander's newest version
- Every figure type in the Skylanders Ultimate NFC Pack V15: Skylanders, traps and villains,
  vehicles, creation crystals, magic items, chests and unreleased figures
- Pauses the game behind the menu in games that leave too little memory for it

**Install:**
- **Peer Pressure:** see **[INSTALL.md](INSTALL.md)**. Unzip the release and drag its two
  folders onto `Hdd1` with FileZilla.
- **BadUpdate / RGH / JTAG (Dashlaunch):** see **[INSTALL-RGH.md](INSTALL-RGH.md)**. Drag
  two folders over, add one line to `launch.ini`, restart. Tested on BadUpdate + XeUnshackle;
  RGH and JTAG run the same Dashlaunch and should work the same way.

**Credits:** see **[CREDITS.md](CREDITS.md)**. This project builds on many people's work.

## Building

The plugin builds with clang + ld.lld + Python, no Xbox 360 SDK needed:

    make -C xrt VARIANT=drawn        # -> xrt/build/drawn/Kaos-Byte2.0v.xex (Peer Pressure)
    make -C xrt VARIANT=rgh          # -> xrt/build/rgh/Kaos-Byte2.0v-RGH.xex (Dashlaunch)
    make -C tests run                # unit tests
    make -C tests emu UNICORN_PY=... UNICORN_LIB=...   # runs the plugin on an emulated CPU

The figure index, pictures and background are made by `tools/mkassets.py` (see its header).
Developer notes from earlier versions are in [docs/TECHNICAL.md](docs/TECHNICAL.md).

## License

MIT (see [LICENSE](LICENSE)); third-party parts keep their own licenses
([THIRD_PARTY.md](THIRD_PARTY.md)). Skylanders is a trademark of Activision; this is an
unofficial fan project.
