# Installing Project Kaos-Byte2.0v (The Static Rift): RGH / JTAG / BadUpdate version

This is the **Dashlaunch** version, for consoles that run Dashlaunch plugins: **RGH**, **JTAG**,
or **BadUpdate + XeUnshackle**. (For a console with **Peer Pressure**, use the other release and
`INSTALL.md` instead.)

> **Tested on BadUpdate + XeUnshackle** in all six games. RGH and JTAG consoles run the same
> Dashlaunch, so they should work the same way. If yours doesn't, please report it with
> `staticrift.log` (and `staticrift-previous.log` after a crash; see the end of this guide).

## What you need

- An RGH / JTAG console, or BadUpdate + XeUnshackle, with **Dashlaunch** working.
- An FTP program such as **FileZilla**, with your console's IP address.
- Any of the six Xbox 360 Skylanders games.

## Install

1. Unzip `Kaos-Byte2.0v-RGH-Release.zip` on your PC.
2. Connect to your console in FileZilla and open **`Hdd1`**.
3. Drag these two folders from the zip onto `Hdd1`:
   - **`Plugins`**: holds `Kaos-Byte2.0v-RGH.xex`. If you already have a `Plugins` folder,
     it's merged in; nothing of yours is replaced.
   - **`The Static Rift`**: the figures, pictures and settings.
4. Add the plugin to Dashlaunch's `launch.ini`. **Which one matters:**
   - **BadUpdate + XeUnshackle:** the `launch.ini` in the root of the **USB stick** you run
     BadUpdate from. A `launch.ini` on the hard drive is *not* read.
   - **RGH / JTAG:** the one Dashlaunch uses: the root of `Hdd1`, or of a USB stick if yours is
     there.

   Open it in Notepad, find the **`[Plugins]`** section and add this line, using the first
   **free** number (`plugin1` to `plugin5`; don't change lines that are already there):

       plugin1 = Hdd:\Plugins\Kaos-Byte2.0v-RGH.xex

   **BadUpdate:** keeping plugins on the USB stick is easier (XeUnshackle recommends it). Copy
   the `Plugins` folder to the root of the stick as well, and use this line instead:

       plugin1 = Usb:\Plugins\Kaos-Byte2.0v-RGH.xex

   Save it back to the same place. (You can also add it from Dashlaunch's own settings program,
   under **Plugins**.)
5. **Restart the console.** Dashlaunch loads plugins when the console starts.
   **BadUpdate:** run BadUpdate + XeUnshackle again as usual; Dashlaunch loads the plugins when
   XeUnshackle's screen closes and you're back on the dashboard.
6. Start a Skylanders game.

## Using it

When the game starts you'll see **"The Static Rift is open"**. **Tap LB + RB** to open the menu,
and tap it again (or press Back) to close it.

| Button | Does |
|---|---|
| D-pad left / right | Previous / next figure (hold to scroll) |
| D-pad up / down | Previous / next section (a folder, or one element within it) |
| LT / RT | Back / forward 10 figures |
| A | Put the figure on the Rift |
| Y | Swap in: replaces the figure(s) of the same kind (Skylander, trap, vehicle or item) |
| B | Take it off |
| X | Clear the Rift |

Figures are listed game by game and folder by folder, sorted by element and then by name.
Everything else works as in the Peer Pressure version: only the figures the game can use are
listed, game progress is saved back into the figure files (with a backup in
`The Static Rift/Backups`), and the game pauses behind the menu in games that leave very little
memory. Figures stay on the Rift when you switch games, like on a real portal.

## Settings

Edit `Hdd1/The Static Rift/staticrift.ini` with Notepad (download it, edit, upload it again).
Changes apply the next time you start a Skylanders game; no restart needed.

| Setting | Default | Meaning |
|---|---|---|
| `[Menu] OnlyCompatible` | 1 | 0 = list figures from later games too |
| `[Menu] NewestOnly` | 1 | 0 = list every series and variant of a Skylander |
| `[Menu] PauseGameWhenLowOnMemory` | 1 | 0 = never pause; the console's pop-up menu is used instead |
| `[Menu] FullScreenMenu` | 1 | 0 = always use the console's plainer pop-up menu |
| `[General] SaveWrites` | 1 | 0 = don't save game progress into the figure files |
| `[General] RememberPortal` | 1 | Keep the same figures on the Rift after a restart |
| `[Controls] Modifier` | LB+RB | The button combo that opens the menu |

## Your own figures

Put any 1 KB figure dump (`.dump` or `.bin`) anywhere inside `The Static Rift/Figures` (up to
five folders deep) and it shows up in the menu.

## If something goes wrong

- **No "The Static Rift is open" message in the game:** delete `StaticRift_trace.txt` from the
  root of `Hdd1`, restart (and on BadUpdate run it again), wait on the dashboard for a minute and
  look again. If no new `StaticRift_trace.txt` appears, Dashlaunch didn't load the plugin: check
  you edited the right `launch.ini` (step 4), the exact path, a free `pluginN` number, and that
  the file really is in `Hdd1/Plugins`.
- **Another plugin stops working, or this one never loads:** two plugins may want the same
  memory address. Try removing other plugins one at a time to find which, and report it.
- **Anything else:** the log is `Hdd1/The Static Rift/staticrift.log`. It records each game you
  play and is started fresh every time the console starts; the one from the session before is
  kept as `staticrift-previous.log`, so after a crash and restart, that's the one to look at.
