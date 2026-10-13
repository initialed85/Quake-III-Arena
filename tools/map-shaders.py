#!/usr/bin/env python3
"""Static asset closure for one Q3 map: BSP shader lump -> shader scripts -> images.

The runtime `fs_logDeps 1` trace only records files the engine opened while traced, so a
map traced from its spawn point ships without the textures used across the level. Every
missing surface then falls back to the default texture and the 3D view renders as flat
grey geometry (browser log: "R_FindImageFile could not find
'textures/sfx/bouncepad01_diamond2cTGA.tga' in shader 'textures/sfx/diamond2cjumppad'").

Walking the BSP's shader lump makes the closure complete by construction: every shader the
map can draw is listed in the BSP.

  tools/map-shaders.py baseq3 q3dm17
  tools/map-shaders.py baseq3 q3dm17 --extract   # also unpack the closure into assets-wasm
"""
import glob
import os
import re
import struct
import subprocess
import sys
import zipfile

LUMP_SHADERS = 1
DSHADER_SIZE = 64 + 8          # char shader[MAX_QPATH], surfaceFlags, contentFlags
IMG_EXTS = (".tga", ".jpg", ".jpeg", ".pcx", ".png")
SCRIPT_EXTS = (".shader", ".mtr")


def bsp_shaders(data, label):
    if data[:4] != b"IBSP":
        raise SystemExit("%s is not an IBSP file" % label)
    version = struct.unpack("<i", data[4:8])[0]
    if version not in (46, 47):
        raise SystemExit("%s: BSP version %i, this tool reads the 46/47 (q3map 2.30) layout"
                         % (label, version))
    # dheader_t: int ident, int version, then 17 lump_t of (fileofs, filelen) - 8 bytes each
    off, length = struct.unpack("<ii", data[8 + 8 * LUMP_SHADERS:8 + 8 * LUMP_SHADERS + 8])
    names = set()
    for i in range(length // DSHADER_SIZE):
        base = off + i * DSHADER_SIZE
        name = data[base:base + 64].split(b"\0", 1)[0].decode("ascii", "replace")
        name = name.strip()
        if not name:
            continue
        # '*' merges the surface into the previous one under this name, '+' is a
        # lightstyle prefix, '-' is nodraw: nothing to draw, nothing to load.
        if name.startswith("-"):
            continue
        names.add(name.lstrip("*+").lower())
    return names


def strip_comments(text):
    return re.sub(r"/\*.*?\*/", " ", re.sub(r"//[^\n]*", "", text, flags=re.S), flags=re.S)


def blocks(text):
    """Yield (name, body) for each top level shader definition."""
    i, n = 0, len(text)
    while i < n:
        brace = text.find("{", i)
        if brace < 0:
            return
        name = text[i:brace].strip().strip('"').strip("'").lower()
        depth, start = 0, brace
        j = brace
        while j < n:
            if text[j] == "{":
                depth += 1
            elif text[j] == "}":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        if name:
            yield name, text[start + 1:j]
        i = j + 1


def images_in(body):
    """Image paths referenced by a shader body: map, image, animMap, skybox sides."""
    out = set()
    body = strip_comments(body)
    for line in body.split("\n"):
        parts = line.split()
        if not parts:
            continue
        key = parts[0].lower()
        if key in ("map", "image", "lightgrid", "envmap", "clampmap") and len(parts) > 1:
            out.add(parts[1].lower())
        elif key == "animmap":
            for tok in parts[1:]:
                out.add(tok.lower())
        elif key == "sky" and len(parts) > 1:
            # "sky <name>" pulls in env<name>_<side>.tga for all six sides
            for side in ("bk", "ft", "lf", "rt", "up", "dn"):
                out.add("env%s_%s" % (parts[1].lower(), side))
    return out


def resolve(image, available):
    """A shader names an image; Q3 strips the extension and takes whichever file exists."""
    image = image.strip().lower()
    if image in available:
        return [image]
    stem = image
    for e in IMG_EXTS:
        if image.endswith(e):
            stem = image[:-len(e)]
            break
    hits = [stem + e for e in IMG_EXTS if stem + e in available]
    if hits:
        return [hits[0]]
    return [image + ".tga"]


def main():
    argv = sys.argv[1:]
    extract = None
    if "--extract" in argv:
        i = argv.index("--extract")
        if i + 1 >= len(argv):
            print("--extract needs a directory")
            return 1
        extract = argv[i + 1]
        argv = argv[:i] + argv[i + 2:]
    if len(argv) < 2:
        print(__doc__)
        return 1
    pk3dir, maps = argv[0], argv[1:]
    if not maps:
        print("no map given")
        return 1

    pks = sorted(glob.glob(os.path.join(pk3dir, "*.pk3")))
    if not pks:
        print("no pk3 files in %s" % pk3dir)
        return 1

    members = {}
    scripts = {}
    location = {}
    for pk3 in pks:
        with zipfile.ZipFile(pk3) as z:
            for info in z.infolist():
                name = info.filename.lower()
                members.setdefault(name, (os.path.basename(pk3), info.compress_size))
                location.setdefault(name, (pk3, info.filename))
                if name.endswith(SCRIPT_EXTS):
                    scripts.setdefault(name, z.read(info.filename).decode("latin-1"))

    def read_member(low):
        pk3, real = location[low]
        with zipfile.ZipFile(pk3) as z:
            return z.read(real)

    wanted_scripts, wanted_images = set(), set()
    parsed = {name: list(blocks(strip_comments(text))) for name, text in scripts.items()}
    parsed_names = {shader for defs in parsed.values() for shader, _ in defs}
    script_of = {shader: name for name, defs in parsed.items() for shader, _ in defs}

    def include(shaders):
        """Pull in the script defining each shader plus every image it references."""
        for shader in shaders:
            name = script_of.get(shader)
            if name is None:
                continue
            wanted_scripts.add(name)
            for s, body in parsed[name]:
                if s == shader:
                    wanted_images.update(images_in(body))

    map_shaders = set()
    for mp in maps:
        low = "maps/%s.bsp" % mp.lower()
        if low not in location:
            print("no %s in any pk3 under %s" % (low, pk3dir))
            return 1
        map_shaders |= bsp_shaders(read_member(low), low)

    # Sky side shaders (skyFt, skyRt, ...) and the default / vertexlight shaders are named
    # by the engine at runtime, not by the BSP.
    include(map_shaders | {"skyft", "skybk", "skylf", "skyrt", "skyup", "skydn", "vertexlight",
                           "_default", "_defaultlightmap", "_empty", "_nomip_default", "clouds",
                           "lightgrid"})

    files = set(wanted_scripts)

    # The map's own files: geometry, bot reachability, level entities, spawn script.
    for mp in maps:
        for ext in ("bsp", "aas", "arena", "ent"):
            files.add("maps/%s.%s" % (mp.lower(), ext))

    # A BSP surface named "textures/base_floor/diamond2c" has no shader definition: Q3
    # auto-creates a shader for the texture of the same name, so the texture is the
    # dependency.
    for shader in map_shaders - parsed_names:
        if shader not in ("noshader", "lightgrid", "_default", "_defaultlightmap", "_empty"):
            files |= set(resolve(shader, members))

    # The cgame and UI name shaders and models in C string literals (rails, projectiles,
    # powerup effects, weapons, players). Those draw at runtime and never appear in the BSP.
    repo = os.path.dirname(os.path.abspath(pk3dir))
    literals = set()
    for src in glob.glob(os.path.join(repo, "code", "**", "*.c"), recursive=True):
        text = open(src, encoding="utf-8", errors="replace").read()
        for lit in re.findall(r'"([^"\n]{4,120})"', text):
            literals.add(lit.strip().lower().lstrip("*+"))
    include(literals & parsed_names)

    # Particle and mark effects build shader names from a base plus a frame number
    # (shaderAnimNames "explode1" -> va("%s%i", base, j) -> explode11..explode19), so a
    # literal is also a prefix of the shaders it registers. Format strings are excluded:
    # "textures/sfx/%s" is a whole directory, not a family of numbered shaders.
    prefix_lits = {l for l in literals if "%" not in l and "/" not in l and len(l) >= 7}
    prefix_shaders = set()
    for shader in parsed_names:
        base = shader.rsplit("/", 1)[-1]
        for n in range(7, len(base) + 1):
            if base[:n] in prefix_lits:
                prefix_shaders.add(shader)
                break
    include(prefix_shaders)

    # An md3 stores its surface shader names as plain text, so the models the code names
    # drag in their own shaders. The code builds those paths with va(), so match the
    # literals as patterns with the format specifiers replaced by wildcards.
    patterns = [re.compile("^%s$" % re.sub(r"%[0-9]*[a-zA-Z]", ".*", re.escape(lit)))
                for lit in literals if "/" in lit and "%" in lit]
    named_models = set()
    for m in members:
        if not m.endswith(".md3"):
            continue
        base = m[:-4]
        if base in literals or any(p.match(base) for p in patterns):
            named_models.add(m)

    # Player models are assembled at runtime from a character name (va("models/players/%s/
    # lower.md3", cvar)), and InitGame registers the default character immediately, so a
    # missing character model is a fatal "DEFAULT_MODEL failed to register" that shuts the
    # server down. A character is in play when its bare name occurs as a literal in the
    # code - the bot list and the character cvar both name them.
    characters = {m.split("/")[2] for m in members
                  if m.startswith("models/players/") and m.count("/") >= 3}
    character_files = set()
    for name in characters:
        if name in literals:
            character_files |= {m for m in members
                                if m.startswith("models/players/%s/" % name)}
    files |= named_models | character_files

    model_shaders = set()
    for m in named_models:
        for s in re.findall(rb"[ -~]{5,}", read_member(m)):
            # md3 surfaces store names like "models/powerups/instant/quad.TGA"; Q3 drops
            # the extension when it looks the shader up
            name = re.sub(r"\.(tga|jpg|png|pcx|bmp|wal|iam|tcr)$", "",
                          s.decode().strip().lower().lstrip("*+"))
            if name in parsed_names:
                model_shaders.add(name)
            elif "/" in name and name.split("/")[0] in ("textures", "models", "gfx", "sprites",
                                                        "ui", "menu", "interface", "sky"):
                # auto-shader: the texture of the same name is the dependency
                files.update(resolve(name, members))
    include(model_shaders)

    # Runtime-named shaders from the code that have no definition are auto-shaders too.
    for lit in literals - parsed_names:
        if "/" in lit and lit.split("/")[0] in ("textures", "models", "gfx", "sprites",
                                                "ui", "menu", "interface", "sky"):
            files.update(resolve(lit, members))

    # Sounds are plain paths in the code and a missing one is fatal, not a warning:
    # snd_mem.c raises "<name> has length 0" for a file it cannot read, which is an
    # ERR_DROP and takes the server down mid-round.
    for lit in literals:
        if lit.startswith("sound/") and lit.endswith(".wav"):
            files.update(resolve(lit, members))

    for image in wanted_images:
        files |= set(resolve(image, members))

    present = sorted(f for f in files if f in members)
    missing = sorted(f for f in files if f not in members)
    total = sum(members[f][1] for f in present)
    print("%i shaders in the BSP, %i files in the closure (%.1f MB compressed), %i not in any pk3"
          % (len(map_shaders), len(present), total / 1048576, len(missing)))
    for f in missing[:10]:
        print("  missing: %s" % f)

    manifest = "/tmp/%s.manifest" % "-".join(maps)
    with open(manifest, "w") as f:
        f.write("\n".join(present) + "\n")
    print("manifest -> %s" % manifest)

    if extract:
        out = extract
        os.makedirs(out, exist_ok=True)
        with open(manifest) as f:
            names = [n.strip() for n in f if n.strip()]
        # Extract through Python: macOS unzip has no "-@" list file and its -n/-o flags
        # conflict, and the archive member names are case sensitive while Q3 looks assets
        # up case insensitively.
        written = skipped = 0
        for low in names:
            if not low or low.endswith("/"):
                continue        # zip directory entries picked up as shader names
            pk3, real = location[low]
            dest = os.path.join(out, low)
            if os.path.exists(dest):
                skipped += 1
                continue
            with zipfile.ZipFile(pk3) as z:
                data = z.read(real)
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with open(dest, "wb") as fh:
                fh.write(data)
            written += 1
        size = subprocess.run(["du", "-sh", out], capture_output=True, text=True).stdout.split()[0]
        print("extracted %i files (%i already present) into %s (%s)" % (
            written, skipped, out, size))
    return 0


sys.exit(main())
