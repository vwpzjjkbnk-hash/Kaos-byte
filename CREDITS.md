# Credits

Project Kaos-Byte2.0v (The Static Rift) is built on other people's work. Thank you all.

## Code and documentation

| Who | What | How it's used | License |
|---|---|---|---|
| [TheBiemGamer/skylanders-portal](https://github.com/TheBiemGamer/skylanders-portal) | Software Portal of Power and the Xbox 360 frame bridge | Ported to C++ in `src/core/portal_core.cpp` | MIT |
| Dolphin, RPCS3 and Cemu emulator teams | Portal of Power emulation (what skylanders-portal was built from) | Indirectly, through the above | GPL-2.0+ |
| [openpak/x360-sdk ("xsdk")](https://github.com/openpak/x360-sdk) by Mena | Open Xbox 360 toolchain: `elf2xex.py`, import thunks, linker script, hardware notes | The no-XDK build in `xrt/` | MIT |
| [Xenia](https://github.com/xenia-project/xenia) and [Xenia Canary](https://github.com/xenia-canary/xenia-canary) | XAM portal API signatures, kernel ordinals, tiled front-buffer addressing, memory call signatures | Reference; the tiled-address formula is ported | BSD |
| [Dolphin Emulator](https://github.com/dolphin-emu/dolphin) | Skylander list (names and elements) | Read by `tools/mkassets.py` at build time only | GPL-2.0+ |
| [Peer Pressure](https://github.com/grimdoomer/Xbox360PeerPressure) by grimdoomer | Xbox 360 softmod with DLL sideloading | Loads the plugin into each game | (see its repo) |
| [RB3Enhanced](https://github.com/RBEnhanced/RB3Enhanced) | Sideload `.ini` format example | The six `.ini` files follow it | (see its repo) |
| [Texthead1/Runes](https://github.com/Texthead1/Runes) | Skylander figure format docs (error byte) | Background for the Error Bypass dumps | (see its repo) |
| Linux kernel radeon driver (AMD / X.Org) | AVIVO display register names (surface address, update lock, flip control) | Reference for the menu's screen handling | GPL-2.0 |
| [QOI](https://qoiformat.org) by Dominic Szablewski | Image format for the menu pictures | Decoder and encoder written from the spec | MIT / CC0 |
| [DejaVu Fonts](https://dejavu-fonts.github.io) (Bitstream Vera) | DejaVu Sans Bold | Rasterized into `src/xbox/overlay_font.h` | Bitstream Vera license |
| [Unicorn Engine](https://www.unicorn-engine.org) | CPU emulator | Test suite only (`tests/emu`), not shipped | GPL-2.0 |
| [Pillow](https://python-pillow.org), [LLVM/clang](https://llvm.org) | Image processing, compiler | Build tools only | HPND, Apache-2.0 |

Full license texts are in [THIRD_PARTY.md](THIRD_PARTY.md).

## Figure dumps

**[Skylanders Ultimate NFC Pack V15](https://github.com/skylandersNFC/Skylanders-Ultimate-NFC-Pack)**
by **SkylandersNFC**, with dumps and fixes from its community:

- **@echoseeker715 (Echo)**: the 31 Special Senseis; Bad Breath, Cy, Sir Hoodington and Earth Quickshot Bebop (3D-printed premade figures)
- **@oddlittlepumpkin**: second Sir Hoodington
- **@beardlydude**: the extra Blue, Bronze, Silver and Gold chests
- **@gibby230, @moltenlavacore, @emberdione**: unreleased creation crystals and Heartbreaker Buckshot
- **Merl008, BoomBringer**: corrected Earth Rune and Fire Angel crystals
- **Texthead, ItsSecretNate, Salty, BoomBringer, Mirakel**: the console Error Bypass dumps
- **@zantzue**: checked the changed dumps; **@Alex~** and **@Elpoyo56**: testing on Switch and PS5
- The community members who made the premade Imaginator characters and their pictures

## Pictures

- **Card art:** the "Mr Shadow" card set and the SkylandersNFC logo cards (creation crystals,
  chests, unreleased), from the
  [Skylanders Image Generator](https://github.com/skylandersNFC/Skylanders-Image-Generator)
  by **SkylandersNFC**. The card artist credited in that set's file names is **Mr Shadow**.
- **Premade creation pictures:** from the Skylanders Ultimate NFC Pack.
- **Menu background, element icons and game logos:** from the project owner's own image
  collection. *Original sources: TODO, fill in before publishing.*

## Skylanders

Skylanders, its characters, artwork and logos are trademarks and property of **Activision**
(developed by Toys for Bob, Vicarious Visions and Beenox). This is an unofficial fan project. It
isn't affiliated with or endorsed by Activision. Use it with games and figures you own.
