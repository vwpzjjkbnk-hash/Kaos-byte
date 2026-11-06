# Installing Project Kaos-Byte4.1.0v (The Static Rift)

This guide is for consoles with **Peer Pressure**. For **RGH / JTAG / BadUpdate + XeUnshackle**
(Dashlaunch), see **[INSTALL-RGH.md](INSTALL-RGH.md)** and use the RGH release.

## What you need

- An Xbox 360 with **Peer Pressure** installed (you should already have
  `Hdd1:\PeerPressure\SideLoading\SideLoad.ini`).
- An FTP program such as **FileZilla**, with your console's IP address.
- Any of the six Xbox 360 Skylanders games (disc, installed or GOD all work).

## 1. Get the right download

On the GitHub page, click **Releases** (on the right), open the newest release and look under
**Assets**. Download **`Kaos-Byte4.1.0v-Release.zip`** (the one without "RGH" in its name).

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

## 3. Install (drag and drop)

1. On your PC, right-click `Kaos-Byte4.1.0v-Release.zip` and choose **Extract All**. Open the new
   `Kaos-Byte4.1.0v` folder: inside are two folders, **`PeerPressure`** and **`The Static Rift`**.
2. In FileZilla, go into that `Kaos-Byte4.1.0v` folder on the **left** side, and double-click
   **`Hdd1`** on the **right** side so you're inside it.
3. On the left, click `PeerPressure`, hold **Ctrl** and click `The Static Rift`, then drag both
   onto the right side. If FileZilla asks about files that already exist, choose **Overwrite**.
   It's about 2,100 files, so give it a few minutes.
   - **`PeerPressure`** adds `KaosByte.xex` and six game `.ini` files to
     `Hdd1/PeerPressure/SideLoading`. Your own `SideLoad.ini` and other files stay as they are.
   - **`The Static Rift`** holds the figures, pictures and settings.
4. Check: on the right you should now have `Hdd1/PeerPressure/SideLoading/KaosByte.xex` (with six
   `.ini` files next to it) and `Hdd1/The Static Rift`.
5. Done. Start a Skylanders game.

Updating from an older version? Use `Kaos-Byte4.1.0v-Update.zip` (about 7 MB: no menu pictures
or figures, but it brings the phone remote's small pictures in `The Static Rift/Phone`): drag its
`PeerPressure` and `The Static Rift` folders onto `Hdd1` and overwrite. Your settings
(`staticrift.ini`), pictures, figures, `picks.ini` and your nicknames and lists (`nicknames.ini`,
`lists.ini`) aren't touched. Then delete the old `Kaos-Byte2.0v.xex` (or
`skydrawn16.xex`) from `Hdd1/PeerPressure/SideLoading`. An old `Hdd1/SkyPortal` folder can be renamed or deleted:
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
| R3 | Next list: **My Picks**, then each game's own figures, then **All** again |

The menu has three kinds of list; **R3** (click the right stick) goes to the next one, and the hint
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

Progress the game saves goes back into the figure file, and the first time a figure is saved a
backup is kept in `The Static Rift/Backups`.

The game pauses while the menu is open, in every game, and carries on when you close it.
(`PauseGame = 0` in `staticrift.ini` keeps it running behind the menu instead.) Pressing the Xbox
Guide button closes the menu and the Guide opens as normal; the menu doesn't open over the Guide.

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

Edit `Hdd1/The Static Rift/staticrift.ini` (download it with FileZilla, edit it in Notepad,
upload it again). The most useful settings:

| Setting | Default | Meaning |
|---|---|---|
| `[Menu] OnlyCompatible` | 1 | 0 = list figures from later games too |
| `[Menu] NewestOnly` | 0 | 1 = ALL lists only each Skylander's newest release |
| `[Menu] PauseGame` | 1 | 0 = the game keeps running behind the menu (except, with the next setting, in games short of memory) |
| `[Menu] PauseGameWhenLowOnMemory` | 1 | 0 = never draw into the game's frames; the console's pop-up menu is used when memory runs out |
| `[Menu] FullScreenMenu` | 1 | 0 = always use the console's plainer pop-up menu |
| `[Menu] SortByType` | 1 | 0 = list figures folder by folder, as the files are laid out |
| `[General] SaveWrites` | 1 | 0 = don't save game progress into the figure files |
| `[General] RememberPortal` | 1 | Keep the same figures on the Rift next time |
| `[General] SwapGapMs` | 150 | Swap in's pause (ms) between the old figure coming off and the new one going on; 0 = as fast as the game takes it, raise it if a game hangs after a swap |
| `[Controls] Modifier` | LB+RB | The button combo that opens the menu |
| `[Remote] Enabled` | 1 | 0 = no phone remote |
| `[Remote] Port` | 80 | The phone remote's port (8080 is used when 80 is taken; the pop-up shows the address to open) |

## Your own figures

Put any 1 KB figure dump (`.dump` or `.bin`) anywhere inside `The Static Rift/Figures` (up to
five folders deep) and it shows up in the menu.

## If something goes wrong

- **The phone can't open the page:** wait for the "Phone remote" pop-up, then type the address
  exactly, with `http://` in front. The phone has to be on your home Wi-Fi (not mobile data or a
  guest network). Some routers keep devices apart ("AP isolation" / "client isolation"): turn that
  off. The log's `Phone remote:` lines say what happened.

- **The phone page says "Not connected":** the game has to be running (the remote stops when you
  quit to the dashboard), and the phone has to be on the same Wi-Fi. It reconnects by itself once
  it can reach the Xbox; pulling down to reload the page also works.

- **No "dll loaded" message:** check that the six `.ini` files and `KaosByte.xex` are in
  `Hdd1/PeerPressure/SideLoading` and that Peer Pressure is working.
- **Anything else:** the log is `Hdd1/The Static Rift/staticrift.log`. It's started fresh every
  time a game starts; the one from the game before is kept as `staticrift-previous.log`, so after
  a crash, that's the one to look at.
