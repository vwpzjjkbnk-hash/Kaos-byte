# skydrawn 1.4 work notes (in progress)

User wants (2026-10-24): skydrawn only; fix glitch (menu flickered: drew after flip, only ~1/3 frames);
tap LB+RB to open, tap again to close (no holding); new UI: element + series + game with logos,
figure images, portal background photo (download.png); organize & place all dumps (NFC pack).

## Design
- Display substitution: VdSwap stub (xrt/entry.S sp_vdswap_hook) — after recording, if
  `sp_menu_current` != 0: r4 = sp_menu_current (6-dword fetch), r8 = sp_menu_current+24 (fb addr word),
  then jump to real VdSwap. Menu frame buffers are our own (MmAllocatePhysicalMemoryEx, WC), same
  w/h/pitch/tiling/format as the game's front buffer; double buffered if memory allows.
- Render in 32-line strips (overlay_render.cpp RenderStrip; one tile row = contiguous block) into a
  small cached strip buffer, then encode+tile into the menu buffer. Background bilinear-scaled.
- Assets on HDD: SkyPortal\UI\background.qoi, UI\elements\<fire..dark>.qoi, UI\games\1-6.qoi,
  UI\figures.idx; Images\<folder>\<name>.qoi (QOI, src/xbox/qoi.cpp decoder).
- figures.idx line: key \t name \t game \t element \t series \t image(rel under Images or -).
  key = rel path components normalized (lower alnum) joined '/', no ext; "*base" fallback lines.
- figures.cpp rewritten: kMaxFigures 2048, depth 5, browse sorted by rel path, GroupJump (up/down),
  index lookup via FNV hash. xrt operator new falls back to xrt_big_alloc (physical) for >16KB.
- New imports: MmAllocatePhysicalMemoryEx 0xBA (6), MmFreePhysicalMemory 0xBD (2).
- Hide all game input while menu open (hooks get_state zeroes pad).
- FATX: names <= 42 chars, no commas/curly quotes -> tools/mkassets.py builds console-safe Figures tree.
- Deliver ONE zip to PC (commit), user extracts & uploads with FileZilla.

## Status (delivered 2026-10-24)
- skydrawn14.xex built, devkit-signed, sha256 9934db71...; unit tests + emulator tests (classic/pop/drawn) pass.
- Delivered to PC: Downloads\skydrawn1.4-part1.zip (xex, 6 inis, skyportal.ini, UI, Figures, Images 1-3 + Eons)
  and skydrawn1.4-part2.zip (Images 4-6). Package source: build/package (1425 files).
- Assets from tools/mkassets.py: 927 dumps, 471 pictures (no pictures exist for villains/crystals/chests).
- Untested on hardware: VdSwap frame substitution (log line "Overlay: menu frames ... -> menu ..." shows the
  addresses used). If the screen goes black on open: compare game fb word/fetch vs menu values in the log.

## 1.5 (2026-10-24)
- Hardware log (Trap Team): ~30 swaps/s recorded, menu frames drawn (237 ms) and handed to VdSwap via
  r4/r8, but nothing showed: the kernel's VdSwap does not take scan-out from those arguments.
- Fix: PinThread in overlay.cpp keeps D1GRPH_PRIMARY_SURFACE_ADDRESS (0x7FC80000 + 0x1844*4) on the
  menu surface's physical address (polls every 1 ms, rewrites when a swap moves it back; remembers the
  kernel's last value and restores it on close). DrawThread frees surfaces only after the pin thread
  has released. Redraw into the hidden surface waits >= 40 ms after publish (vblank latch).
  r4/r8 substitution kept (harmless). Log: "Overlay: scan-out address register ..." at first open,
  "menu was on screen N ms; scan-out moved back ... X time(s)" on close.
- Figure picture: rounded rectangle sized to the picture's aspect (box 520x332 at 720p), element-colour
  border + GlowRect, DrawImageRounded. No-picture: card-shaped white panel with icon/logo.
- Delivered Downloads\skydrawn1.5-update.zip (xex + 6 inis + INSTALL.txt), assets unchanged from 1.4.

## 1.6 (2026-10-24)
- Hardware 1.5: menu shows, but a band of the game showed at the top of the screen (scan-out
  register updates immediately, mid-frame; the 1 ms pin thread re-pointed it ~5 ms late).
- Fix: hook the game's VdSetGraphicsInterruptCallback (0x1D5) import. sp_gfx_irq_hook (entry.S)
  calls Direct3D's handler, then writes sp_pin_phys to 0x7FC86110 in the same interrupt. The pin
  thread stays as a fallback, now time-critical (KeSetBasePriorityThread 0x99, PCR+0x100).
  The log reports the handler address, interrupt count, and re-points made in the interrupt.
- Figure filter: figures.idx column 7 holds the first game; "@<toy id>\t<game>" lines classify the
  user's own dumps. Rule: first game = the earliest game folder with that character ID, but no
  earlier than 4 for IDs 500-599 (sidekicks). FigureLibrary::SetGameFilter(running game) is applied
  in SortBrowse. Setting: [Menu] OnlyCompatible (default 1). Run mkassets.py --first-games <dir> to
  rewrite an existing index.
- Delivered Downloads\skydrawn1.6-update.zip (xex, 6 inis, UI\figures.idx).
