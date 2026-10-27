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
| D-pad up / down | Previous / next folder |
| LT / RT | Back / forward 10 figures |
| A | Put the figure on the Rift |
| Y | Swap in (only this figure on the Rift) |
| B | Take it off |
| X | Clear the Rift |

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
| `[General] SaveWrites` | 1 | 0 = don't save game progress into the figure files |
| `[General] RememberPortal` | 1 | Keep the same figures on the Rift next time |
| `[Controls] Modifier` | LB+RB | The button combo that opens the menu |

## Your own figures

Put any 1 KB figure dump (`.dump` or `.bin`) anywhere inside `The Static Rift/Figures` (up to
five folders deep) and it shows up in the menu.

## If something goes wrong

- **No "dll loaded" message:** check that the six `.ini` files and `Kaos-Byte2.0v.xex` are in
  `Hdd1/PeerPressure/SideLoading` and that Peer Pressure is working.
- **Anything else:** the log is `Hdd1/The Static Rift/staticrift.log`. It's rewritten every
  time a game starts, so copy it before starting the next game.
