# Installing Project Kaos-Byte4.1.0v (The Static Rift): RGH / JTAG / BadUpdate version

This is the **Dashlaunch** version, for consoles that run Dashlaunch plugins: **RGH**, **JTAG**,
or **BadUpdate + XeUnshackle**. (For a console with **Peer Pressure**, use the other release and
`INSTALL.md` instead.)

> **Since 4.0.0v the RGH version has everything the Peer Pressure one has:** the phone remote (pick
> figures from your phone), nicknames (START in the menu, or on the phone), Favorites and your own
> lists. The menu was tested on BadUpdate + XeUnshackle in all six games, and the phone remote on
> RGH. RGH, JTAG and BadUpdate consoles run the same Dashlaunch, so they should all work the same
> way. If something doesn't, please report
> it with `staticrift.log` (and `staticrift-previous.log` after a crash; see the end of this guide).

## What you need

- An RGH / JTAG console, or BadUpdate + XeUnshackle, with **Dashlaunch** working.
- An FTP program such as **FileZilla**, with your console's IP address.
- Any of the six Xbox 360 Skylanders games.

## 1. Get the right download

On the GitHub page, click **Releases** (on the right), open the newest release and look under
**Assets**. Download **`Kaos-Byte4.1.0v-RGH-Release.zip`** (with "RGH" in its name). The other zip is for Peer Pressure and
has no `Plugins` folder.

Don't download **Source code** or use the green **Code** button: that's the code for building the
plugin, not the plugin itself. Nothing goes inside your game's folder either: the plugin lives in
its own folder on the hard drive.

## 2. First time using FileZilla?

FileZilla copies files from your PC to the Xbox over your home network.

1. Install **FileZilla Client** (free, filezilla-project.org) on your PC.
2. Turn on the Xbox and go to your homebrew dashboard (Aurora, FSD...). Its FTP server only runs
   while the dashboard is up, and the PC and the Xbox must be on the same network.
3. Find the Xbox's IP address (it looks like `192.168.1.23`): Aurora shows it on its main screen,
   or on the Xbox go to Settings > System > Network Settings > your network > Configure Network.
4. In FileZilla's **Quickconnect** bar at the top, enter: **Host** the Xbox's IP, **Username** and
   **Password** `xboxftp` (Aurora; `xbox` on FSD and most others), **Port** `21`. Click
   **Quickconnect**.
5. The **left** side is your PC and the **right** side is the Xbox. On the right you'll see drives
   like `Hdd1` (the hard drive), `Usb0` and `Flash` (never change anything in `Flash`).
6. Drag from left to right to copy to the Xbox, and from right to left to copy from it. Wait until
   **Queued files** at the bottom is empty. If **Failed transfers** lists anything, right-click
   in it and requeue them.

## 3. Install

1. On your PC, right-click `Kaos-Byte4.1.0v-RGH-Release.zip` and choose **Extract All**. Open the
   new `Kaos-Byte4.1.0v-RGH` folder: inside are two folders, **`Plugins`** and **`The Static
   Rift`**. The plugin itself, `KaosByteDSL.xex`, is inside `Plugins`.
2. In FileZilla, go into that `Kaos-Byte4.1.0v-RGH` folder on the **left** side, and double-click
   **`Hdd1`** on the **right** side so you're inside it.
3. On the left, click `Plugins`, hold **Ctrl** and click `The Static Rift`, then drag both onto
   the right side. It's about 2,600 files, so give it a few minutes.
   - **`Plugins`** holds `KaosByteDSL.xex`. If you already have a `Plugins` folder, it's merged
     in; nothing of yours is replaced.
   - **`The Static Rift`** holds the figures, pictures (the menu's and the phone's) and settings.
4. Add the plugin to Dashlaunch's `launch.ini`. **Which one matters:**
   - **BadUpdate + XeUnshackle:** the `launch.ini` in the root of the **USB stick** you run
     BadUpdate from. A `launch.ini` on the hard drive is *not* read.
   - **RGH / JTAG:** the one Dashlaunch uses: the root of `Hdd1`, or of a USB stick if yours is
     there.

   Drag `launch.ini` from the right side of FileZilla to the left to copy it to your PC, then
   right-click it and choose **Open with > Notepad**. Find the **`[Plugins]`** section and add
   this line, using the first **free** number (`plugin1` to `plugin5`; don't change lines that are
   already there):

       plugin1 = Hdd:\Plugins\KaosByteDSL.xex

   **BadUpdate:** keeping plugins on the USB stick is easier (XeUnshackle recommends it). Copy
   the `Plugins` folder to the root of the stick as well, and use this line instead:

       plugin1 = Usb:\Plugins\KaosByteDSL.xex

   Type it exactly like that: a space on each side of `=`, and `Hdd:` (not `Hdd1:`). Save it
   (**Ctrl+S**), then drag it from the left side back to the same place on the right and choose
   **Overwrite**. (You can also add it from Dashlaunch's own settings program, under
   **Plugins**.)

   **Updating from an older version?** Use `Kaos-Byte4.1.0v-RGH-Update.zip` (about 7 MB: no menu
   pictures or figures, but it brings the phone remote's small pictures in `The Static Rift/Phone`):
   drag its `Plugins` and `The Static Rift` folders onto `Hdd1` and overwrite. Your settings
   (`staticrift.ini`), pictures, figures, `picks.ini` and your nicknames and lists aren't touched
   (the phone remote is on without a `[Remote]` section; copy the one in the full zip's
   `staticrift.ini` into yours to change it). Your `launch.ini` line stays the same. The plugin
   used to be called `Kaos-Byte2.0v-RGH.xex`: if your line still says that, change it to
   `KaosByteDSL.xex`, and delete the old file.
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
| D-pad up / down | Previous / next section (a type and element, like "Skylanders / Magic") |
| LT / RT | Back / forward 10 figures |
| A | Put the figure on the Rift |
| Y | Swap in: replaces the figure(s) of the same kind (Skylander, trap, vehicle or item) |
| B | Take it off |
| X | Clear the Rift |
| START | Give the figure a nickname (an on-screen keyboard; see below) |
| R3 | Next list: **Favorites** and your own lists, **My Picks**, each game's own figures, then **All** again |

The menu has four kinds of list; **R3** (click the right stick) goes to the next one, and the hint
at the bottom of the menu says which:

1. **ALL**: every figure the game you're playing can use (in Imaginators, all 1,169 in the pack),
   every release. By type: **gimmick Skylanders** (Giants, Swappers, Trap Masters, SuperChargers,
   Senseis, and their variants), **Skylanders**, **creation crystals**, **traps**, **vehicles**,
   **magic items**, then **adventure packs**. Within each type by element, then by name, so an
   Eon's Elite or a variant sits with the other Skylanders of its element. D-pad up/down jumps
   from one type and element to the next.
2. **MY PICKS**: your rosters from `The Static Rift/picks.ini` (one section per game; edit it in
   Notepad). You get the roster of the game you're playing first, then the earlier games' rosters,
   newest game first. In Imaginators that's your Imaginators roster, then SuperChargers, Trap
   Team, Swap Force, Giants and Spyro's Adventure. In Spyro's Adventure it's only that roster.
   Each roster is sorted like ALL, and the heading at the top says whose roster you're in
   ("GIANTS ROSTER / SKYLANDERS / FIRE"). A SuperCharger you pick brings its vehicle with it.
   After the rosters come every creation crystal, trap, vehicle, magic item and adventure pack
   the game can use, in that order ("MY PICKS / TRAPS / AIR").
   To pick one release of a figure, put its game in front or its series after it: `Trap Team
   Eon's Elite Chop Chop`, `Drobot (Series 2)`. A name that's slightly misspelled still finds
   its figure, and the log lists any pick nothing matched.
3. **One game at a time** (Spyro's Adventure, then Giants, and so on): only the figures that came
   out in that game (everything in that game's folder, every release, unreleased ones included),
   sorted like ALL. After the last game, R3 goes back to ALL.
4. **FAVORITES and your own lists**, made on the phone (below): right after ALL, each in the order
   you added the figures (lists with nothing in them are skipped).

**Nicknames:** press **START** on a figure and a keyboard comes up: D-pad to move, **A** to type the
key (DEL deletes, CAPS for capitals, DONE saves), **B** deletes a letter, **X** a space, **Y**
capitals, **START** saves, **BACK** cancels. Up to 20 letters; it shows under the figure's name, in
the menu and on the phone. Save it empty to take it away.

Everything else works as in the Peer Pressure version: only the figures the game can use are
listed, game progress is saved back into the figure files (with a backup in
`The Static Rift/Backups`), and the game pauses while the menu is open, in every game (the Xbox
Guide button closes the menu). Figures stay on the Rift when you switch games, like on a real
portal.

## The phone remote

Pick figures from a phone, tablet or PC while you play, on the same Wi-Fi as the Xbox:

1. Start a Skylanders game. About 10 seconds in, a pop-up says **"Phone remote: open
   http://192.168.1.23"** (your Xbox's address). It also shows under THE STATIC RIFT at the top of
   the menu.
2. Open that address in the phone's browser. ("Add to Home Screen" makes it open like an app.)

The page shows what's on the Rift (✕ takes a figure off, **Clear all** empties it), a search box,
and the same lists as the menu (All, My Picks, each game). Tap a figure for **Put on the Rift**,
**Swap in** or **Take off**. Whatever you do on the phone or with the menu shows on both, straight
away.

**Nicknames, Favorites and lists.** Tap a figure, then **+ Add a nickname** under its name: the
nickname shows on its card between the name and the series / game, and on the TV. The **☆ Favorite**
button stars it (Favorites get their own tab), and **☰ Lists** puts it in lists of your own or makes
a new one. **+ New list** at the end of the tabs makes an empty one; each list gets its own tab with
**Rename** and **Delete**. On the TV, press **START** on a figure for an on-screen keyboard to give it
a nickname (D-pad and A to type, B delete, X space, Y capitals, START save, BACK cancel), and R3
goes through Favorites and your lists as well. Everything is kept in `The Static Rift/nicknames.ini`
and `lists.ini` (the figure files aren't changed, and updates don't touch these two).

**⚡ Quick tap** (next to Clear all) is for speedruns: one tap on a figure swaps it in at once, with
no details screen. Hold a figure (right-click on a PC) to see its details instead. The phone
remembers the setting.

The page keeps one live connection to the Xbox and catches up by itself after a Wi-Fi hiccup or
when you come back from the lock screen. The dot at the top shows it: green is connected, yellow
is reconnecting (a second or two), red is not connected. It's built to work in any phone browser
(Safari, Chrome, Firefox, Samsung Internet...), and only the pictures near the screen are kept, so
long lists don't weigh the phone down.

The Xbox itself serves the page (the plugin has a small web server built in), so it stays on your
home network: nothing goes on the internet, and there's nothing to install or pay for. It only
works while a Skylanders game is running, and the game keeps running while you use it. The page
is `The Static Rift/UI/remote.html` if you want to change how it looks.

The phone gets small copies of the card art from `The Static Rift/Phone` (about 6 KB each, made by
`tools/mkphone.py` from the menu's pictures). A picture you added yourself still shows on the phone,
just slower, until you run `tools/mkphone.py "<The Static Rift folder>"` to make its small copy.

## Settings

Edit `Hdd1/The Static Rift/staticrift.ini` with Notepad (download it, edit, upload it again).
Changes apply the next time you start a Skylanders game; no restart needed.

| Setting | Default | Meaning |
|---|---|---|
| `[Menu] OnlyCompatible` | 1 | 0 = list figures from later games too |
| `[Menu] NewestOnly` | 0 | 1 = ALL lists only each Skylander's newest release |
| `[Menu] PauseGame` | 1 | 0 = the game keeps running behind the menu (except, with the next setting, in games short of memory) |
| `[Menu] PauseGameWhenLowOnMemory` | 1 | 0 = never draw into the game's frames; the console's pop-up menu is used when memory runs out |
| `[Menu] FullScreenMenu` | 1 | 0 = always use the console's plainer pop-up menu |
| `[Menu] SortByType` | 1 | 0 = list figures folder by folder, as the files are laid out |
| `[General] SaveWrites` | 1 | 0 = don't save game progress into the figure files |
| `[General] RememberPortal` | 1 | Keep the same figures on the Rift after a restart |
| `[General] SwapGapMs` | 150 | Swap in's pause (ms) between the old figure coming off and the new one going on; 0 = as fast as the game takes it, raise it if a game hangs after a swap |
| `[Controls] Modifier` | LB+RB | The button combo that opens the menu |
| `[Remote] Enabled` | 1 | 0 = no phone remote |
| `[Remote] Port` | 80 | The phone remote's port (8080 is used when 80 is taken; the pop-up shows the address to open) |

## Your own figures

Put any 1 KB figure dump (`.dump` or `.bin`) anywhere inside `The Static Rift/Figures` (up to
five folders deep) and it shows up in the menu.

## If something goes wrong

- **The phone can't open the page:** wait for the "Phone remote" pop-up (about 10 seconds into
  the game), then type the address exactly, with `http://` in front. The phone has to be on your
  home Wi-Fi (not mobile data or a guest network). Some routers keep devices apart ("AP
  isolation" / "client isolation"): turn that off. The remote runs only while a Skylanders game is
  running. The log's `Phone remote:` lines say what happened; if one says "the console refused a
  plain (insecure) socket", your console's patches don't allow the remote's kind of connection:
  please report it with the log.

- **No "The Static Rift is open" message in the game:** delete `StaticRift_trace.txt` from the
  root of `Hdd1`, restart (and on BadUpdate run it again), wait on the dashboard for a minute and
  look again. If no new `StaticRift_trace.txt` appears, Dashlaunch didn't load the plugin: check
  you edited the right `launch.ini` (step 4), the exact path, a free `pluginN` number, and that
  the file really is in `Hdd1/Plugins`.
- **"Portal Disconnected" in the game:** another plugin (RGH "stealth" plugins do this) may hook
  the way the game finds the portal before the Static Rift sees the game. The Static Rift works
  around that by itself; the log then says "pointing xam's exports at it instead". If the game
  still says the portal is disconnected, send the log (below).
- **Another plugin stops working, or this one never loads:** two plugins may want the same
  memory address. Try removing other plugins one at a time to find which, and report it.
- **Anything else:** the log is `Hdd1/The Static Rift/staticrift.log`. It records each game you
  play and is started fresh every time the console starts; the one from the session before is
  kept as `staticrift-previous.log`, so after a crash and restart, that's the one to look at.
