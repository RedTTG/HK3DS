#!/usr/bin/env python3
"""Pack exported room objects (build_objs.py output) for the 3DS runtime.
   python pack_objs.py <objdir> <romfs> Room [Room ...]
For each room: romfs/rooms/<Room>.obj (zlib-wrapped JSON) and <Room>_objs.spr (every clip of every tk2d library the
room's objects use, named "<guid8>:<clip>"), plus romfs/sfx/<clip>.pcm (22050 Hz mono s16) for audio the FSMs play.
"""

import sys, os, json, zlib, struct, subprocess

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hkcommon import metamap, EX
from build_sprites import SpriteSet
from tk2d import load_library

SKIP_ROOTS = ()  # every boss runs its own FSMs now


def wrap(d):
    return b"HKZ1" + struct.pack("<I", len(d)) + zlib.compress(d, 9)


def audio_names(x, acc):
    if isinstance(x, dict):
        if "audio" in x and isinstance(x["audio"], str):
            acc.add(x["audio"])
        if x.get("k") == "as" and isinstance(x.get("clip"), str):
            acc.add(x["clip"])  # AudioSource clips
        for v in x.values():
            audio_names(v, acc)
    elif isinstance(x, list):
        for v in x:
            audio_names(v, acc)


def convert_audio(names, sfxdir):
    am = {
        os.path.splitext(f)[0]: f
        for f in os.listdir(f"{EX}/AudioClip")
        if f.endswith((".ogg", ".wav"))
    }
    n = 0
    for nm in sorted(names):
        out = f"{sfxdir}/{nm}.pcm"
        if os.path.exists(out) or nm not in am:
            continue
        src = f"{EX}/AudioClip/{am[nm]}"
        r = subprocess.run(
            [
                "ffmpeg",
                "-v",
                "error",
                "-y",
                "-i",
                src,
                "-ac",
                "1",
                "-ar",
                "22050",
                "-f",
                "s16le",
                out,
            ]
        )
        if r.returncode == 0:
            n += 1
    return n


def drop_skipped_roots(tree):
    """Remove boss roots that still use the hand-written controllers."""
    objs = tree["objects"]
    keep = [True] * len(objs)
    for i, o in enumerate(objs):
        p = o["p"]
        if p == -1:
            keep[i] = not (SKIP_ROOTS and str(o["n"]).startswith(SKIP_ROOTS))
        else:
            keep[i] = keep[p]
    if all(keep):
        return tree
    remap, new = {}, []
    for i, o in enumerate(objs):
        if keep[i]:
            remap[i] = len(new)
            new.append(o)
    for o in new:
        if o["p"] != -1:
            o["p"] = remap[o["p"]]

    # FSM/object references {"o": i}
    def fix(x):
        if isinstance(x, dict):
            if "o" in x and isinstance(x["o"], int) and len(x) == 1:
                return {"o": remap[x["o"]]} if x["o"] in remap else {"none": True}
            return {k: fix(v) for k, v in x.items()}
        if isinstance(x, list):
            return [fix(v) for v in x]
        return x

    tree["objects"] = fix(new)
    tree["fsms"] = fix(tree["fsms"])
    return tree


def pack(objdir, romfs, room):
    p = f"{objdir}/{room}.obj.json"
    if not os.path.exists(p):
        return
    tree = json.load(open(p))
    tree = drop_skipped_roots(tree)
    if not tree["objects"]:
        for ext in (".obj", "_objs.spr"):
            if os.path.exists(f"{romfs}/rooms/{room}{ext}"):
                os.remove(f"{romfs}/rooms/{room}{ext}")
        print(room, "no objects")
        return
    gm = metamap("GameObject")
    # OBJ_ONLY=1: rewrite only <Room>.obj (objects/FSMs changed, art didn't); sprites are looked up by name at runtime
    obj_only = os.environ.get("OBJ_ONLY") and os.path.exists(
        f"{romfs}/rooms/{room}_objs.spr"
    )
    if obj_only:
        libs = {}
        for g in tree["libs"]:
            lp = gm.get(g)
            lib = load_library(lp) if lp else None
            if lib:
                libs[g] = sorted(k for k in lib if k)
        tree["libs"] = libs
        names = set()
        audio_names(tree, names)
        tree["audio"] = sorted(names)
        open(f"{romfs}/rooms/{room}.obj", "wb").write(
            wrap(json.dumps(tree, separators=(",", ":")).encode())
        )
        n = convert_audio(names, f"{romfs}/sfx")
        print(room, "obj only", len(tree["objects"]), "new pcm", n)
        return
    ss = SpriteSet(0.25)
    libs = {}
    skip_libs = set(os.environ.get("SKIP_LIBS", "").split(",")) - {""}
    for g in tree["libs"]:
        if g[:8] in skip_libs:
            continue
        lp = gm.get(g)
        if not lp:
            continue
        lib = load_library(lp)
        if not lib:
            continue
        try:
            ss.add_library(lp, set(k for k in lib if k), g[:8] + ":")
        except Exception as ex:
            print("  lib failed", g, ex)
            continue
        libs[g] = sorted(k for k in lib if k)
    tree["libs"] = libs
    # Sprite assets (SpriteRenderers, sprites FSMs switch to): named "sp:<guid12>", 16 px per world unit
    import bake_scene as B
    from texpack import premul_resize

    for g in tree.get("sprites", []):
        sa = B.sprite_asset(g)
        r = B.sprite_image(sa) if sa else None
        if not r:
            continue
        im, offx, offy, RW, RH, pvx, pvy, ppu = r
        k = 16.0 / (ppu or 100)
        w, h = max(1, round(im.width * k)), max(1, round(im.height * k))
        if w > 1024 or h > 1024:
            continue
        im2 = premul_resize(im, w, h)
        ox = round((offx - pvx) * k)
        oy = round(-(offy + im.height - pvy) * k)
        ss.add_named("sp:" + g[:12], im2, ox, oy)
    # tk2dSprites with no animator: one fixed frame each, "tk:<coll8>:<id>"
    seen = set()
    for tr in [tree] + [v for v in tree["prefabs"].values() if v]:
        for o in tr["objects"]:
            ks = [c.get("k") for c in o["c"]]
            if "sprite" not in ks or "anim" in ks:
                continue
            c = [c for c in o["c"] if c.get("k") == "sprite"][0]
            g, sid = c.get("coll"), int(c.get("id") or 0)
            if not g or (g, sid) in seen:
                continue
            seen.add((g, sid))
            try:
                fi = ss.add_tk2d_frame(g, sid)
            except Exception as ex:
                print("  tk2d frame failed", g, sid, ex)
                continue
            ss.names.append(("tk:%s:%d" % (g[:8], sid), fi))
    # frames a script switches a tk2dSprite to (Tk2dSpriteSetId: the King's Pass great door cracking), by id or name
    import tk2d

    tkname = {}
    for tr in [tree] + [v for v in tree["prefabs"].values() if v]:
        owner = {}
        for o in tr["objects"]:
            for c in o["c"]:
                if c.get("k") == "fsm":
                    owner[c.get("i")] = o
        for fi_, f in enumerate(tr.get("fsms") or []):
            o = owner.get(fi_)
            sc = [c for c in (o or {}).get("c", []) if c.get("k") == "sprite"]
            if not sc or not sc[0].get("coll"):
                continue
            g = sc[0]["coll"]
            for st in f["states"]:
                for a in st["actions"]:
                    if not a["type"].endswith("Tk2dSpriteSetId"):
                        continue
                    pr = a.get("params") or {}
                    if pr.get("gameObject", {}).get("owner") is not True:
                        continue
                    nm, sid = pr.get("ORSpriteName"), pr.get("spriteID")
                    if isinstance(nm, str) and nm:
                        coll = tk2d.collection_by_guid(g)
                        ids = [
                            i
                            for i, sp in enumerate((coll or {}).get("sprites") or [])
                            if sp.get("name") == nm
                        ]
                        if not ids:
                            continue
                        sid = ids[0]
                        tkname[f"{g[:8]}:{nm}"] = sid
                    if not isinstance(sid, int) or (g, sid) in seen:
                        continue
                    seen.add((g, sid))
                    try:
                        fi = ss.add_tk2d_frame(g, sid)
                    except Exception as ex:
                        print("  tk2d frame failed", g, sid, ex)
                        continue
                    ss.names.append(("tk:%s:%d" % (g[:8], sid), fi))
    if tkname:
        tree["tkname"] = tkname
    os.makedirs(f"{romfs}/rooms", exist_ok=True)
    if ss.frames:
        ss.write(f"{romfs}/rooms/{room}_objs.spr")
        if True:  # RGBA4 with dithering: half the size on the SD card and in memory
            import spr_to_rgba4

            spr_to_rgba4.convert(
                f"{romfs}/rooms/{room}_objs.spr",
                keep8=(0,) if room == "_Knight" else (),
            )  # the Knight's glows and spell effects page stays 8-bit
    names = set()
    audio_names(tree, names)
    tree["audio"] = sorted(names)
    open(f"{romfs}/rooms/{room}.obj", "wb").write(
        wrap(json.dumps(tree, separators=(",", ":")).encode())
    )
    n = convert_audio(names, f"{romfs}/sfx")
    print(
        room,
        "objects",
        len(tree["objects"]),
        "libs",
        len(libs),
        "audio",
        len(names),
        "new pcm",
        n,
    )


if __name__ == "__main__":
    objdir, romfs = sys.argv[1], sys.argv[2]
    for r in sys.argv[3:]:
        pack(objdir, romfs, r)
