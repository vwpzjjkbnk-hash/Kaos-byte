#!/usr/bin/env python3
"""Builds the drawn menu's assets and a console-ready figure folder from the user's files.

    python3 tools/mkassets.py --pack <NFC pack dir> --images <image dir> --dolphin Skylander.cpp \
        --out <dir> [--saves <dir of loose dumps>]

Writes, under <out>/The Static Rift:
  Figures/<game>/<category>/...   the pack's dumps, renamed where the console's FATX file system
                                  needs it (names up to 42 characters, no commas or curly quotes)
  Images/<folder>/<name>.qoi      each figure picture, at most 320 px, as QOI
  UI/background.qoi               the portal background
  UI/elements/<element>.qoi       round element icons (cut from the icon sheet)
  UI/games/<1-6>.qoi              game logos with transparent backgrounds
  UI/figures.idx                  one line per dump: key, name, game, element, series, picture,
                                  first game it works in, character id, release kind (R = a
                                  regular release, V = a variant of one, - = neither: traps,
                                  crystals, items...); "@<character id>" lines: first game
and report.txt (pictures it couldn't match, renamed files).

figures.idx keys are the dump's path below Figures, folder by folder, lower-case letters and
digits only, joined with "/" and without the extension (src/xbox/figures.cpp makes the same key);
"*<file name>" lines are fallbacks for dumps kept outside the pack folders. Elements come from the
dump's folder (Creation Crystals and Traps are sorted by element), Dolphin's Skylander table
(GPL-2.0-or-later, Dolphin Emulator Project; only character names and elements are used), the
Trap Team villain folders, and the Imaginators Sensei list.
"""
import argparse
import json
import os
import re
import shutil
import sys

from PIL import Image

ELEMENTS = ["", "Air", "Earth", "Fire", "Water", "Magic", "Tech", "Life", "Undead", "Light", "Dark"]
EL_INDEX = {n.lower(): i for i, n in enumerate(ELEMENTS) if n}
EL_INDEX["kaos"] = EL_INDEX["dark"]
GAMES = {1: "Spyro's Adventure", 2: "Giants", 3: "Swap Force", 4: "Trap Team", 5: "SuperChargers",
         6: "Imaginators"}
SENSEIS = {  # Imaginators Senseis (skylanderswiki.com/wiki/Sensei) and guest stars
    "Air Strike": "Air", "Ambush": "Life", "Aurora": "Light", "Barbella": "Earth",
    "Boom Bloom": "Life", "Buckshot": "Magic", "Chain Reaction": "Tech", "Chopscotch": "Undead",
    "Ember": "Fire", "Flarewolf": "Fire", "Flare Wolf": "Fire", "King Pen": "Water",
    "Mysticat": "Magic", "Pit Boss": "Undead", "Ro-Bow": "Tech", "Starcast": "Dark",
    "Tidepool": "Water", "Tri-Tip": "Earth", "Wild Storm": "Air", "Crash Bandicoot": "Life",
    "Dr. Neo Cortex": "Tech",
    "VVind Up": "Tech",  # Gear Head VVind Up (unreleased Wind-Up variant)
}
# Category folder (without its "N) " prefix), lower case -> series text shown in the menu.
CATEGORY = {
    "figures": "Series 1", "giants": "Giant", "new (series 1)": "Series 1", "lightcores": "LightCore",
    "magic items": "Magic Item", "adventure packs": "Adventure Pack", "battle arena": "Battle Arena",
    "expansion packs": "Expansion Pack", "promotional items": "Promo Item", "chests": "Treasure Chest",
    "in-game variants": "In-Game Variant", "legendaries": "Legendary", "sidekicks": "Sidekick",
    "chase variants": "Chase Variant", "swappers": "Swapper", "unreleased": "Unreleased",
    "trap masters": "Trap Master", "minis": "Mini", "eon's elite": "Eon's Elite", "traps": "Trap",
    "crystal traps": "Trap", "crystal trap variants": "Trap Variant",
    "trappable villains": "Villain", "trappable villain variants": "Villain Variant",
    "superchargers": "SuperCharger", "in-game variant superchargers": "In-Game Variant",
    "vehicles": "Vehicle", "air vehicles": "Sky Vehicle", "land vehicles": "Land Vehicle",
    "sea vehicles": "Sea Vehicle", "in-game variant vehicles": "In-Game Variant",
    "senseis": "Sensei", "in-game variant senseis": "In-Game Variant",
    "villain senseis": "Villain Sensei", "in-game variant villain senseis": "In-Game Variant",
    "creation crystals": "Creation Crystal", "elements": "Creation Crystal",
    "premade characters": "Premade Creation",
}
PRIORITY = ["chase variants", "legendaries", "eon's elite", "lightcores", "minis"]
# Picture names that differ from the dump names in ways the rules below don't cover.
ALIASES = {
    "polarwhirlwind": "polars2whirlwind", "royaldoubletrouble": "s2royaldoubletrouble",
    "legendaryignitor": "legendarys2ignitor", "legendaryslambam": "legendarys2slambam",
    "legendarystealthelf": "legendarys2stealthelf", "drobotlightcore": "lcdrobot",
    "skyironshield": "ironshield", "sparxdragonfly": "sparx", "eliteghostroaster": "eliteghoastroaster",
    "scorpionstrikercatapult": "scorpionstriker", "missiletowdiveclops": "missletoedivecl0ps",
    "gearheadvvindup": "vvindup", "eyesmall": "eyesmall", "minijini": "minijini",
    "december": "ember", "doubletrouble15": "doubletrouble",
}
VARIANT_WORDS = ["legendary", "dark", "gold", "golden", "silver", "bronze", "chrome", "crystal",
                 "clear", "glow", "in", "the", "metallic", "purple", "pearl", "red", "blue", "green",
                 "flocked", "white", "stone", "sparkle", "polar", "patina", "pink", "orange", "nitro",
                 "jade", "enchanted", "power", "steel", "plated", "employee", "edition", "s2", "s3",
                 "s4", "lc", "lightcore", "springtime", "halloween", "pumpkin", "flame", "frito-lay",
                 "heavy", "metal", "happy", "birthday", "ben", "rock", "candy"]
FATX_OK = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 !#$%&'()-.@[]^_`{}~")


def norm(s):
    s = s.replace("’", "'").replace("‘", "'").replace("&", "and").lower()
    return re.sub(r"[^a-z0-9]+", "", s)


def key_of(rel_parts, base_only=False):
    stem = os.path.splitext(rel_parts[-1])[0]
    if base_only:
        return "*" + norm(stem)
    return "/".join([norm(p) for p in rel_parts[:-1]] + [norm(stem)])


def toy_id(path):
    """The character id a dump carries (block 1, little-endian), the same for every release of a
    character (Series 2-4, LightCore, Eon's Elite, chase and in-game variants)."""
    with open(path, "rb") as f:
        d = f.read(0x20)
    return d[0x10] | d[0x11] << 8


def first_games(figures_dir):
    """{character id: first game (1-6) it works in}. Figures work in the game they came out with
    and every later one; a character re-released in a later game (Series 2, LightCores, Eon's
    Elite...) still works back to its first game. Sidekicks (ids 500-599, given away with Spyro's
    Adventure and Giants) are only playable from Trap Team on, as Minis."""
    first = {}
    for root, dirs, files in os.walk(figures_dir):
        rel = os.path.relpath(root, figures_dir)
        m = re.match(r"(\d)\.", rel)
        if not m:
            continue
        for f in files:
            if f.lower().endswith(".dump"):
                t = toy_id(os.path.join(root, f))
                first[t] = min(first.get(t, 9), int(m.group(1)))
    for t in first:
        if 500 <= t < 600:
            first[t] = max(first[t], 4)
    return first


# Releases of a character (by series label): a newer regular release replaces the older releases
# and their variants in the menu (src/xbox/figures.cpp, [Menu] NewestOnly). Everything else -
# traps, villains, creation crystals, vehicles, items, chests, unreleased figures - is left alone.
REGULAR = {"Series 1", "Series 2", "Series 3", "Series 4", "Giant", "Swapper Top", "Swapper Bottom",
           "Trap Master", "SuperCharger", "Sensei", "Villain Sensei", "Sidekick", "Mini"}
VARIANT = {"Chase Variant", "In-Game Variant", "Legendary", "LightCore", "Eon's Elite"}


def release_kind(series):
    return "R" if series in REGULAR else "V" if series in VARIANT else "-"


def character_families(rows):
    """{character id: family id}. A character is its own family, except SuperChargers' new
    versions of older Skylanders ("Deep Dive Gill Grunt"), which have a character id of their own
    but replace the older figures in the games that take them (SuperChargers, Imaginators)."""
    core = {}  # character id -> its name without the version words ("Gill Grunt")
    for fields, g, t, kind in rows:
        if kind == "R" and int(fields[2] or 0) <= 4 and t not in core:
            core[t] = re.sub(r"\s*\([^)]*\)", "", fields[1]).strip()
    family = {}
    for fields, g, t, kind in rows:
        if kind != "R" or fields[4] != "SuperCharger" or t in core:
            continue
        name = re.sub(r"\s*\([^)]*\)", "", fields[1]).strip()
        best = max((c for c in core.items() if name.endswith(" " + c[1])), key=lambda c: len(c[1]),
                   default=None)
        if best:
            family[t] = best[0]
    return family


def add_first_games(sp_dir):
    """Adds the first-game, character id and release kind columns and the "@<character id>" lines
    to <sp_dir>/UI/figures.idx."""
    figures = os.path.join(sp_dir, "Figures")
    first = first_games(figures)
    by_key, by_base = {}, {}
    for root, dirs, files in os.walk(figures):
        dirs.sort()
        for f in sorted(files):
            if not f.lower().endswith(".dump"):
                continue
            parts = os.path.relpath(os.path.join(root, f), figures).split(os.sep)
            t = toy_id(os.path.join(root, f))
            g = first[t] if re.match(r"\d\.", parts[0]) else 0
            by_key[key_of(parts)] = (g, t)
            by_base.setdefault(key_of(parts, base_only=True), (g, t))
    path = os.path.join(sp_dir, "UI", "figures.idx")
    out = []
    for line in open(path, encoding="ascii").read().splitlines():
        if not line or line.startswith("#"):
            if line.startswith("#"):
                line = re.sub(r"series, picture(, first game)? \(",
                              "series, picture, first game, character id, release kind (", line)
            out.append(line)
            continue
        if line.startswith("@"):
            continue
        fields = line.split("\t")[:6]
        g, t = by_key.get(fields[0]) or by_base.get(fields[0]) or (0, 0)
        kind = release_kind(fields[4]) if g else "-"
        out.append([fields, g, t, kind])
    family = character_families([o for o in out if isinstance(o, list)])
    out = ["\t".join(o[0] + [str(o[1]), str(family.get(o[2], o[2])), o[3]])
           if isinstance(o, list) else o for o in out]
    out += [f"@{t}\t{g}" for t, g in sorted(first.items())]
    with open(path, "w", newline="\n", encoding="ascii") as f:
        f.write("\n".join(out) + "\n")
    return first


def strip_prefix(folder):
    return re.sub(r"^\d+\)\s*", "", folder).strip()


def fatx(name, is_file, taken):
    """A name the console's FATX file system accepts: at most 42 characters, plain ASCII."""
    s = name.replace("’", "'").replace("‘", "'").replace("“", "").replace("”", "")
    s = s.replace(",", "").replace("+", " and ")
    s = "".join(c for c in s if c in FATX_OK)
    s = re.sub(r"\s+", " ", s).strip()
    stem, ext = (os.path.splitext(s) if is_file else (s, ""))
    # Too long: abbreviate, then drop "(...)", then cut.
    for long, short in (("Employee Edition", "EE"), ("Frito-Lay ", ""), ("Glow-in-the-Dark", "GITD"),
                        ("Heartbreaker Buckshot", "HB Buckshot"), ("Max Level ", "Max "),
                        (" Path)", ")"), ("Legendary", "Leg."), ("Variant", "Var."),
                        ("Metallic", "Met."), ("Mystery", "Myst.")):
        if len(stem) + len(ext) <= 42:
            break
        stem = stem.replace(long, short)
    if len(stem) + len(ext) > 42:
        stem = re.sub(r"\s*\([^)]*\)", "", stem).strip() or stem
    stem = stem[:42 - len(ext)].rstrip()
    out = stem + ext
    n = 2
    while out.lower() in taken:
        suffix = f" {n}"
        out = stem[:42 - len(ext) - len(suffix)].rstrip() + suffix + ext
        n += 1
    taken.add(out.lower())
    return out


# ---- QOI encoder (qoiformat.org) ----

def qoi_encode(img):
    img = img.convert("RGBA")
    w, h = img.size
    px = img.tobytes()
    out = bytearray(b"qoif" + w.to_bytes(4, "big") + h.to_bytes(4, "big") + bytes([4, 0]))
    index = [(0, 0, 0, 0)] * 64
    prev = (0, 0, 0, 255)
    run = 0
    n = w * h
    for i in range(n):
        p = (px[4 * i], px[4 * i + 1], px[4 * i + 2], px[4 * i + 3])
        if p == prev:
            run += 1
            if run == 62 or i == n - 1:
                out.append(0xC0 | (run - 1))
                run = 0
            continue
        if run:
            out.append(0xC0 | (run - 1))
            run = 0
        h_ = (p[0] * 3 + p[1] * 5 + p[2] * 7 + p[3] * 11) % 64
        if index[h_] == p:
            out.append(h_)
        else:
            index[h_] = p
            if p[3] == prev[3]:
                vr = (p[0] - prev[0] + 128) % 256 - 128
                vg = (p[1] - prev[1] + 128) % 256 - 128
                vb = (p[2] - prev[2] + 128) % 256 - 128
                vg_r, vg_b = vr - vg, vb - vg
                if -3 < vr < 2 and -3 < vg < 2 and -3 < vb < 2:
                    out.append(0x40 | (vr + 2) << 4 | (vg + 2) << 2 | (vb + 2))
                elif -33 < vg < 32 and -9 < vg_r < 8 and -9 < vg_b < 8:
                    out.append(0x80 | (vg + 32))
                    out.append((vg_r + 8) << 4 | (vg_b + 8))
                else:
                    out += bytes([0xFE, p[0], p[1], p[2]])
            else:
                out += bytes([0xFF, p[0], p[1], p[2], p[3]])
        prev = p
    out += bytes([0] * 7 + [1])
    return bytes(out)


def save_qoi(img, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(qoi_encode(img))


# ---- elements ----

def dolphin_elements(path):
    names = {}
    for name, element in re.findall(r'\{"([^"]+)",\s*Game::\w+,\s*Element::(\w+)', open(path).read()):
        if element in ("Other",):
            continue
        base = re.sub(r"\s*\([^)]*\)", "", name).strip()
        names.setdefault(norm(base), element)
    return names


def element_for(parts, display, dolphin, villains):
    stem_words = display.replace("(", " ").replace(")", " ").split()
    for folder in parts[:-1]:  # an element folder anywhere in the path
        e = EL_INDEX.get(strip_prefix(folder).lower())
        if e:
            return e
    lower_path = " ".join(parts).lower()
    if "traps" in lower_path or "creation crystals" in lower_path:
        for w in stem_words:
            e = EL_INDEX.get(w.lower())
            if e:
                return e
    n = norm(display)
    best, best_len = 0, 0
    for table in (villains, {norm(k): v for k, v in SENSEIS.items()}, dolphin):
        for name, element in table.items():
            if len(name) > best_len and name and name in n:
                e = EL_INDEX.get(element.lower())
                if e:
                    best, best_len = e, len(name)
        if best:
            return best
    return 0


def series_for(parts, display):
    m = re.search(r"\(Series (\d)\)", parts[-1])
    if m:
        return f"Series {m.group(1)}"
    stem = os.path.splitext(parts[-1])[0]
    # Category folders; a folder named after the figure itself (a swapper's "Blast Zone" folder)
    # is not a category, but numbered folders ("7) Eon's Elite") always are.
    cats = [strip_prefix(p) for p in parts[1:-1]
            if re.match(r"^\d+\)", p) or not stem.lower().startswith(p.lower())]
    cats = [c for c in cats if c.lower() not in EL_INDEX]
    lower = [c.lower() for c in cats]
    label = ""
    for p in PRIORITY:
        if p in lower:
            label = CATEGORY[p]
            break
    if not label:
        for c in reversed(lower):
            if c in CATEGORY:
                label = CATEGORY[c]
                break
            if c.startswith("returning"):
                label = "Returning"
                break
    if label == "Swapper" or "swappers" in lower:
        if stem.endswith(" Top"):
            label = "Swapper Top"
        elif stem.endswith(" Bottom"):
            label = "Swapper Bottom"
    return label


# ---- pictures ----

def picture_candidates(stem, swappers):
    s = stem.replace("’", "'")
    base = re.sub(r"\s*\([^)]*\)", "", s).strip()
    out = [s, base]
    m = re.search(r"\(Series (\d)\)", s)
    if m:
        out.append(f"S{m.group(1)} {base}")
    inner = re.findall(r"\(([^)]*)\)", s)
    if inner:
        out.append("Trap " + inner[0])
        out.append("Trap " + inner[0] + " 1")
    out.append("Trap " + base)
    if base.startswith("Eon's Elite "):
        out.append("Elite " + base[len("Eon's Elite "):])
    words = base.split()
    if any(w.lower() == "lightcore" for w in words):
        rest = [w for w in words if w.lower() != "lightcore"]
        out.append("Lightcore " + " ".join(rest))
    if base.endswith(" Mini"):
        out.append(base[:-5])
    out.append("Vehicle " + base)
    for part in ("Top", "Bottom"):
        if base.endswith(" " + part):
            for sw in swappers:
                a, b = sw.split(" ", 1)
                if sw in base:
                    out.append(base.replace(sw, a if part == "Top" else b))
    if "Blue Chest" in base:
        out.append("Blue Imaginite Chest")
    expanded = []
    for c in out:
        expanded.append(c)
        k = ALIASES.get(norm(c))
        if k:
            expanded.append("@" + k)
    return expanded


def dropped_variants(stem):
    """The name with leading variant words dropped one at a time ("Gold Fire Kraken Top" ->
    "Fire Kraken Top", ...); the first word always goes ("Special Air Strike", "Corrupted ...")."""
    words = re.sub(r"\s*\([^)]*\)", "", stem.replace("’", "'")).split()
    return [" ".join(words[k:]) for k in range(1, min(4, len(words)))
            if words[k - 1].lower().strip("-") in VARIANT_WORDS or k == 1]


def match_picture(stem, game_folder, by_folder, by_key_all, swappers, fuzzy=True):
    cands = picture_candidates(stem, swappers)
    if fuzzy:
        for s in dropped_variants(stem):
            cands += picture_candidates(s, swappers)
    for c in cands:
        key = c[1:] if c.startswith("@") else norm(c)
        for folder in (game_folder, "! Eons Elite"):
            hit = by_folder.get(folder, {}).get(key)
            if hit:
                return hit
        hit = by_key_all.get(key)
        if hit:
            return hit
    return None


# ---- card covers (preferred over the older pictures) ----

COVER_PREFIX = re.compile(r"^(Mr Shadow's|SkylandersNFC's)\s+")
# Trap Team's villain-variant dumps are named after the villain; their cards after the trap.
COVER_ALIASES = {
    "redhottusslesprout": "earthtotemredhotspinningsandstorm",
    "riotshieldshredder": "lifetorchriotemeraldenergy",
    "steamedbroccoliguy": "lifetoucansteamedoakeagle",
    "rebellobgoblin": "lightrocketrebelshiningship",
    "steampunkshrednaught": "techflyinghelmetsteampunkmakersmana",
    "outlawbrawlandchain": "waterlogholderoutlawwetwalter",
    "december": "ember", "doubletrouble15": "doubletrouble",
}


def index_covers(dirs):
    """{game: {key: [(landscape, folder keys, file)]}} for card pictures laid out by game
    ("01_Spyro/...", "1. Spyro's Adventure/..."). Names lose the card set's "Mr Shadow's " prefix;
    a trap card naming its villain ("Dark Spider (Shadow Spider) [Eye Scream]") is found by the
    trap and by the villain. Landscape cards (SuperCharged vehicles) only stand in when there is
    no portrait card of the figure."""
    covers = {}
    for d in dirs:
        for root, dirs_, files in os.walk(d):
            dirs_.sort()
            for f in sorted(files):
                if not f.lower().endswith((".jpg", ".jpeg", ".png", ".webp")):
                    continue
                rel = os.path.relpath(os.path.join(root, f), d).split(os.sep)
                m = re.match(r"0?(\d)[._ ]", rel[0])
                if not m:
                    continue
                game = int(m.group(1))
                name = COVER_PREFIX.sub("", os.path.splitext(f)[0])
                name = re.sub(r"^(Vehicle )?Supercharged\s+", "", name)
                with Image.open(os.path.join(root, f)) as im:
                    landscape = im.width > im.height
                folders = [norm(strip_prefix(p)) for p in rel[1:-1]]
                plain = re.sub(r"\s*\[[^\]]*\]", "", name)
                keys = [norm(name), norm(plain)]
                keys += [norm(b) for b in re.findall(r"\[([^\]]*)\]", name)]
                keys.append(norm(re.sub(r"\s*\([^)]*\)", "", plain)))  # "Cynder (Series 2)"
                for k in dict.fromkeys(keys):
                    covers.setdefault(game, {}).setdefault(k, []).append(
                        (landscape, folders, os.path.join(root, f)))
    return covers


def cover_names(stem, parts):
    """Names a figure's card may have: the dump name, without its copy number ("Air Angel 3",
    "Bronze Mystery Chest 12"), without "(...)", without " Mini", and the figure's own folder."""
    s = stem.replace("’", "'").strip()
    base = re.sub(r"\s*\([^)]*\)", "", s).strip()
    out = [s, re.sub(r"\s+\d+(?= \()", "", s),  # "Blue Chest 1 (Cursed Tiki Temple)"
           re.sub(r"\s+\d+$", "", s)]
    parent = parts[-2] if len(parts) >= 3 else ""
    if parent and not re.match(r"^\d+\)", parent) and strip_prefix(parent).lower() not in EL_INDEX:
        out.append(parent)
    out += [base, re.sub(r"\s+\d+$", "", base)]
    if base.endswith(" Mini"):
        out.append(base[:-5])
    return list(dict.fromkeys(out))


def match_cover(stem, parts, game, covers, fuzzy=False, other_games=False):
    """The card for a dump, from its own game's cards, or with other_games from the other
    games' (nearest earlier game first: a Giants chase variant of a Spyro's Adventure figure)."""
    if other_games:
        for g in list(range(game - 1, 0, -1)) + list(range(game + 1, 7)):
            hit = match_cover(stem, parts, g, covers, fuzzy)
            if hit:
                return hit
        return None
    table = covers.get(game, {})
    if not table:
        return None
    names = cover_names(stem, parts)
    if fuzzy:
        names = [n for s in dropped_variants(stem) for n in cover_names(s, parts[:-1] + [s])]
    folders = [norm(strip_prefix(p)) for p in parts[1:-1]]
    for n in names:
        k = norm(n)
        hits = table.get(COVER_ALIASES.get(k, k)) or table.get(k)
        if hits:
            best = min(hits, key=lambda h: (h[0], -sum(a == b for a, b in zip(h[1], folders))))
            return best[2]
    return None


def side_picture(src):
    """A picture kept next to the dump with the same name (the pack's premade creations)."""
    stem = os.path.splitext(src)[0]
    for ext in (".webp", ".png", ".jpg", ".jpeg"):
        if os.path.exists(stem + ext):
            return stem + ext
    return None


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--first-games":
        # Update an existing output's figures.idx: mkassets.py --first-games "<out>/The Static Rift"
        first = add_first_games(sys.argv[2])
        print(f"{len(first)} characters")
        return
    ap = argparse.ArgumentParser()
    ap.add_argument("--pack", required=True)
    ap.add_argument("--images", required=True)
    ap.add_argument("--dolphin", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--saves", default="")
    ap.add_argument("--covers", action="append", default=[],
                    help="card pictures by game folder, preferred over --images (repeatable)")
    args = ap.parse_args()
    out = os.path.join(args.out, "The Static Rift")
    shutil.rmtree(out, ignore_errors=True)
    report = []

    # Pictures available, by folder and normalized name.
    by_folder, by_key_all = {}, {}
    for folder in sorted(os.listdir(args.images)):
        fp = os.path.join(args.images, folder)
        if not os.path.isdir(fp):
            continue
        for f in sorted(os.listdir(fp)):
            if f.lower().endswith((".jpg", ".jpeg", ".png")):
                k = norm(os.path.splitext(f)[0])
                by_folder.setdefault(folder, {})[k] = (folder, f)
                by_key_all.setdefault(k, (folder, f))
    pic_folder_for_game = {}
    for folder in by_folder:
        m = re.match(r"(\d)\.", folder)
        if m:
            pic_folder_for_game[int(m.group(1))] = folder

    dolphin = dolphin_elements(args.dolphin)

    # Dumps in the pack, with Trap Team's villain folders giving villain elements.
    dumps = []
    for top in sorted(os.listdir(args.pack)):
        m = re.match(r"(\d)\.", top)
        if not m or not os.path.isdir(os.path.join(args.pack, top)):
            continue
        for root, dirs, files in os.walk(os.path.join(args.pack, top)):
            dirs.sort()
            for f in sorted(files):
                if f.lower().endswith(".dump") and os.path.getsize(os.path.join(root, f)) == 1024:
                    rel = os.path.relpath(os.path.join(root, f), args.pack)
                    dumps.append((int(m.group(1)), rel.split(os.sep)))
    villains = {}
    swappers = set()
    for game, parts in dumps:
        lower = [p.lower() for p in parts]
        if any("trappable villains" in p for p in lower):
            folder = strip_prefix(parts[-2]).lower()
            if folder in EL_INDEX:
                stem = re.sub(r"\s*\([^)]*\)", "", os.path.splitext(parts[-1])[0]).strip()
                villains[norm(stem)] = folder.capitalize()
        if len(parts) >= 3 and strip_prefix(parts[1]).lower() == "swappers":
            swappers.add(parts[2])

    lines = ["# The Static Rift figures.idx: key, name, game, element, series, picture (tools/mkassets.py)"]
    # (add_first_games adds the first-game column and the character lines afterwards)
    base_lines = {}
    converted = {}  # source picture -> Images path
    taken_pics, used_pics, stats = {}, set(), {}
    covers = index_covers(args.covers)
    unmatched = []
    taken_by_dir = {}
    for game, parts in dumps:
        # Console-safe path.
        new_parts = []
        for i, p in enumerate(parts):
            parent = "/".join(new_parts)
            taken = taken_by_dir.setdefault(parent, {})
            if p in taken:
                new_parts.append(taken[p])
                continue
            safe = fatx(p, i == len(parts) - 1, set(taken.values()) | {v.lower() for v in taken.values()})
            taken[p] = safe
            if safe != p:
                report.append(f"renamed: {'/'.join(parts[:i + 1])} -> {safe}")
            new_parts.append(safe)
        src = os.path.join(args.pack, *parts)
        dst = os.path.join(out, "Figures", *new_parts)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(src, dst)

        stem = os.path.splitext(parts[-1])[0].strip()
        display = re.sub(r"\s*\(Series \d\)", "", stem.replace("’", "'")).strip()
        element = element_for(parts, display, dolphin, villains)
        series = series_for(parts, display)
        # Pictures, best first: the card covers, a picture kept beside the dump, another game's
        # card, the older pictures by name, then cards and older pictures with variant words
        # dropped ("Gold Chop Chop" -> "Chop Chop").
        game_folder = pic_folder_for_game.get(game, "")
        old = lambda fuzzy: match_picture(stem, game_folder, by_folder, by_key_all, swappers, fuzzy)
        pic, kind = None, ""
        for kind, find in (("card", lambda: match_cover(stem, parts, game, covers)),
                           ("side", lambda: side_picture(src)),
                           ("card*", lambda: match_cover(stem, parts, game, covers, other_games=True)),
                           ("old", lambda: old(False)),
                           ("card~", lambda: match_cover(stem, parts, game, covers, fuzzy=True)),
                           ("card*~", lambda: match_cover(stem, parts, game, covers, True, True)),
                           ("old~", lambda: old(True))):
            pic = find()
            if pic:
                break
        pic_rel = "-"
        if pic:
            if kind.startswith("old"):
                pic = os.path.join(args.images, *pic)
                label = os.path.basename(os.path.dirname(pic))
            elif kind == "side":
                label = "Premade"
            else:
                label = f"Cards {game}"
            src_kind = re.sub(r"[*~]", "", kind)
            stats[src_kind] = stats.get(src_kind, 0) + 1
            if kind != "card":
                report.append(f"picture ({kind}): {'/'.join(parts)} <- {os.path.basename(pic)}")
            pic_rel = converted.get(pic)
            if not pic_rel:
                name = COVER_PREFIX.sub("", os.path.splitext(os.path.basename(pic))[0])
                taken = taken_pics.setdefault(label, set())
                pic_rel = fatx(label, False, set()) + "\\" + fatx(name, False, taken) + ".qoi"
                im = Image.open(pic).convert("RGB")
                im.thumbnail((320, 320), Image.LANCZOS)
                save_qoi(im, os.path.join(out, "Images", *pic_rel.split("\\")))
                converted[pic] = pic_rel
            used_pics.add(pic)
        else:
            unmatched.append("/".join(parts))
        fields = [display, str(game), str(element), series, pic_rel]
        lines.append("\t".join([key_of(new_parts)] + fields))
        bk = key_of(new_parts, base_only=True)
        base_lines.setdefault(bk, fields)

    # Loose dumps kept from before (the user's saves): fall back to the file name.
    lines += ["\t".join([k] + v) for k, v in base_lines.items()]
    os.makedirs(os.path.join(out, "UI"), exist_ok=True)
    with open(os.path.join(out, "UI", "figures.idx"), "w", newline="\n", encoding="ascii",
              errors="replace") as f:
        f.write("\n".join(lines) + "\n")

    add_first_games(out)
    build_ui(args.images, out)
    report += [f"no picture: {u}" for u in unmatched]
    all_covers = {h[2] for g in covers.values() for hs in g.values() for h in hs}
    unused = sorted(all_covers - used_pics)
    report += [f"card not used: {u}" for u in unused]
    with open(os.path.join(args.out, "report.txt"), "w") as f:
        f.write("\n".join(report) + "\n")
    print(f"{len(dumps)} dumps, {len(converted)} pictures, {len(unmatched)} without a picture; "
          f"figures by picture source: {stats}; {len(all_covers) - len(unused)} of "
          f"{len(all_covers)} cards used")


def build_ui(images_dir, out):
    ui = os.path.join(out, "UI")
    # Background: the portal photo, cropped to 16:9.
    bg = Image.open(os.path.join(images_dir, "download.png")).convert("RGB")
    w, h = bg.size
    ch = w * 9 // 16
    # 480x270 (500 KB once decoded on the console, where memory is tight in some games; the menu
    # scales it up smoothly and darkens it, so the lower resolution hardly shows).
    bg = bg.crop((0, (h - ch) // 2, w, (h - ch) // 2 + ch)).resize((480, 270), Image.LANCZOS)
    save_qoi(bg, os.path.join(ui, "background.qoi"))
    # Element icons: column 4 of the icon sheet, rows fire, water, air, earth, life, undead, tech,
    # magic, light, dark; cut to a circle.
    sheet = Image.open(os.path.join(images_dir, "47d3b04a14113dcfc1c936f365d5ebb4.png")).convert("RGB")
    order = ["fire", "water", "air", "earth", "life", "undead", "tech", "magic", "light", "dark"]
    cw, chh = sheet.width / 6.0, sheet.height / 10.0
    for row, name in enumerate(order):
        box = (int(3 * cw) + 2, int(row * chh) + 2, int(4 * cw) - 2, int((row + 1) * chh) - 2)
        icon = sheet.crop(box).resize((128, 128), Image.LANCZOS).convert("RGBA")
        mask = Image.new("L", (512, 512), 0)
        from PIL import ImageDraw
        ImageDraw.Draw(mask).ellipse((10, 10, 502, 502), fill=255)
        icon.putalpha(mask.resize((128, 128), Image.LANCZOS))
        save_qoi(icon, os.path.join(ui, "elements", name + ".qoi"))
    # Game logos.
    logos = {1: "8710pafjhx2g1.png", 2: "fandzgkyc23g1.png", 3: "skylanders-swap-force-logo.png",
             4: "Skylanders-Trap-Team-Logo.png", 5: "Skylanders_SuperChargers_render_logo.png",
             6: "skylanders-imaginators-unused-logos-v0-a3xee9ez6cqh1.png"}
    for game, f in logos.items():
        im = Image.open(os.path.join(images_dir, f)).convert("RGBA")
        im = remove_flat_background(im)
        bbox = im.getbbox()
        if bbox:
            im = im.crop(bbox)
        im.thumbnail((220, 100), Image.LANCZOS)  # drawn at most about this size
        save_qoi(im, os.path.join(ui, "games", f"{game}.qoi"))


def remove_flat_background(im):
    """Makes a flat white or black backdrop around a logo transparent (flood fill from the edges)."""
    px = im.load()
    w, h = im.size
    if all(px[x, y][3] < 250 for x, y in ((0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1))):
        return im  # already transparent around the edges
    ref = px[0, 0]
    def close(c):
        return abs(c[0] - ref[0]) + abs(c[1] - ref[1]) + abs(c[2] - ref[2]) < 60
    seen = set()
    stack = [(x, y) for x in range(w) for y in (0, h - 1)] + [(x, y) for y in range(h) for x in (0, w - 1)]
    while stack:
        x, y = stack.pop()
        if (x, y) in seen or not (0 <= x < w and 0 <= y < h):
            continue
        seen.add((x, y))
        c = px[x, y]
        if not close(c):
            continue
        px[x, y] = (c[0], c[1], c[2], 0)
        stack += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    return im


if __name__ == "__main__":
    main()
