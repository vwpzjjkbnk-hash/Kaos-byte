# Installing Project Kaos-Byte2.0v (The Static Rift)

This guide is for consoles with **Peer Pressure**. For **RGH / JTAG / BadUpdate + XeUnshackle**
(Dashlaunch), see **[INSTALL-RGH.md](INSTALL-RGH.md)** and use the RGH release.

## What you need

- An Xbox 360 with **Peer Pressure** installed (you should already have
  `Hdd1:\PeerPressure\SideLoading\SideLoad.ini`).
- An FTP program such as **FileZilla**, with your console's IP address.
- Any of the six Xbox 360 Skylanders games (disc, installed or GOD all work).

## Install (drag and drop)

1. Unzip `Kaos-Byte2.0v-Release.zip` on your PC.
2. Connect to your console in FileZilla and open **`Hdd1`** on the right-hand side.
3. Drag these two folders from the zip onto `Hdd1`:
   - **`PeerPressure`**: adds `Kaos-Byte2.0v.xex` and six game `.ini` files to
     `Hdd1/PeerPressure/SideLoading`. If FileZilla asks, overwrite. Your own `SideLoad.ini`
     and other files stay as they are.
   - **`The Static Rift`**: the figures, pictures and settings.
4. Done. Start a Skylanders game.

Updating from an older version? Delete the old `skydrawn16.xex` from
`Hdd1/PeerPressure/SideLoading`. An old `Hdd1/SkyPortal` folder can be renamed or deleted:
The Static Rift uses its own folder when both are there.

## Using it

When a game starts you'll see Peer Pressure's "dll loaded" message, then
**"The Static Rift is open"**.

**Tap LB + RB** to open the menu. Tap again (or press Back) to close it.

| Button | Does |
|---|---|
| D-pad left / right | Previous / next figure (hold to scroll) |
| D-pad up / down | Previous / next section (a type and element, like "Skylanders / Magic") |
| LT / RT | Back / forward 10 figures |
| A | Put the figure on the Rift |
| Y | Swap in: replaces the figure(s) of the same kind (Skylander, trap, vehicle or item) |
| B | Take it off |
| X | Clear the Rift |
| R3 | Next list: **My Picks**, then each game's own figures, then all figures again |

Figures are listed by type, wherever their files are in the pack: **gimmick Skylanders**
(Giants, Swappers, Trap Masters, SuperChargers, Senseis, and their variants), **creation
crystals**, **Skylanders**, **vehicles**, **traps**, **adventure packs**, then **magic items**.
Within each type they're sorted by element, then by name. So an Eon's Elite or a variant sits with
the other Skylanders of its element, not in a far-away folder. (`SortByType = 0` puts back the
folder-by-folder order.)

**My Picks (R3):** press R3 (click the right stick) in the menu to switch to your picks for the
game you're playing, and again to go back to everything. Your picks are in
`The Static Rift/picks.ini` (one section per game; edit it in Notepad). The picks list shows each
Skylander you named, plus every crystal, vehicle, trap, adventure pack and magic item the game can
use, in the same order. Every unreleased figure the game can use is in it too (Instant, Special
and Corrupted figures, Heartbreaker Buckshot, VVind-Up, Goldie...), each with the regular figure
it's a version of (Instant Food Fight brings Food Fight, Special Air Strike brings Air Strike).
To pick one release of a figure, put its game in front or its series after it: `Trap Team Eon's
Elite Chop Chop`, `Drobot (Series 2)`. A name that's slightly misspelled still finds its figure,
and the log lists any pick nothing matched.

**One game's figures (R3 again):** after My Picks, R3 shows only the figures from one game at a
time (Spyro's Adventure's, then Giants', and so on: every release in that game's folder, for the
games whose figures the running game can use), then goes back to all figures. The hint at the
bottom of the menu says which list R3 shows next.

The menu only lists figures the game you're playing can use, and only each Skylander's newest
version. Progress the game saves goes back into the figure file, and the first time a figure
is saved a backup is kept in `The Static Rift/Backups`.

In games that leave very little memory (Giants, Swap Force, Imaginators) the game pauses while
the menu is open, and carries on when you close it.

## Settings

Edit `Hdd1/The Static Rift/staticrift.ini` (download it with FileZilla, edit it in Notepad,
upload it again). The most useful settings:

| Setting | Default | Meaning |
|---|---|---|
| `[Menu] OnlyCompatible` | 1 | 0 = list figures from later games too |
| `[Menu] NewestOnly` | 1 | 0 = list every series and variant of a Skylander |
| `[Menu] PauseGameWhenLowOnMemory` | 1 | 0 = never pause; the console's pop-up menu is used instead |
| `[Menu] FullScreenMenu` | 1 | 0 = always use the console's plainer pop-up menu |
| `[Menu] SortByType` | 1 | 0 = list figures folder by folder, as the files are laid out |
| `[General] SaveWrites` | 1 | 0 = don't save game progress into the figure files |
| `[General] RememberPortal` | 1 | Keep the same figures on the Rift next time |
| `[Controls] Modifier` | LB+RB | The button combo that opens the menu |

## Your own figures

Put any 1 KB figure dump (`.dump` or `.bin`) anywhere inside `The Static Rift/Figures` (up to
five folders deep) and it shows up in the menu.

## If something goes wrong

- **No "dll loaded" message:** check that the six `.ini` files and `Kaos-Byte2.0v.xex` are in
  `Hdd1/PeerPressure/SideLoading` and that Peer Pressure is working.
- **Anything else:** the log is `Hdd1/The Static Rift/staticrift.log`. It's started fresh every
  time a game starts; the one from the game before is kept as `staticrift-previous.log`, so after
  a crash, that's the one to look at.
