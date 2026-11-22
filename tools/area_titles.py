#!/usr/bin/env python3
"""Area Title Controllers of every scene -> romfs/text/area_titles.txt
One line per controller: room, Area Event, Display Right, Door Trigger, Only On Revisit, Unvisited Pause, Visited Pause,
Wait for Trigger, trigger box (x0 y0 x1 y1 or '-'). The runtime ports AreaTitleController.cs around these values.
   python area_titles.py <out file> [rooms...]"""

import sys, os, subprocess

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fsm_extract as F
import build_room as BR

X = BR.X


def scan(path, room):
    objs = F.load_objs(path)
    P = F.Paths(objs)
    out = []
    for k, (t, v) in objs.items():
        if (
            t != "MonoBehaviour"
            or not isinstance(v.get("fsm"), dict)
            or v["fsm"].get("name") != "Area Title Controller"
        ):
            continue
        gid = int(v["m_GameObject"]["fileID"])
        go = objs.get(gid, (None, {}))[1]
        if str(go.get("m_IsActive", 1)) == "0":
            continue
        f = F.decode_fsm(v, P, "")
        V = f.get("variables") or {}

        def g(n, d):
            x = V.get(n)
            return d if not x or x.get("value") is None else x["value"]

        out.append(
            dict(
                path=P.path(gid),
                ev=g("Area Event", ""),
                right=bool(g("Display Right", False)),
                door=g("Door Trigger", "") or "",
                rev=bool(g("Only On Revisit", False)),
                up=float(g("Unvisited Pause", 2)),
                vp=float(g("Visited Pause", 2)),
                trig=bool(g("Wait for Trigger", False)),
            )
        )
    if out and any(o["trig"] for o in out):
        sc = X.Scene(X.load_scene(path))
        cols = X.colliders(sc)
        for o in out:
            c = next((c for c in cols if c["path"] == o["path"] and c["trigger"]), None)
            o["box"] = c["box"] if c else None
    return out


def main(outp, rooms=None):
    if not rooms:
        r = subprocess.run(
            ["grep", "-rlE", "Area Title Controller", "--include=*.unity", BR.EX],
            capture_output=True,
            text=True,
        ).stdout.split("\n")
        paths = [(os.path.basename(p)[:-6], p) for p in r if p]
    else:
        paths = [(n, BR.scene_path(n)) for n in rooms]
    lines = []
    for room, p in sorted(paths):
        try:
            res = scan(p, room)
        except Exception as e:
            print("fail", room, e)
            continue
        for o in res:
            b = o.get("box")
            bs = "%.2f %.2f %.2f %.2f" % tuple(b) if (o["trig"] and b) else "-"
            lines.append(
                "\t".join(
                    [
                        room,
                        o["ev"] or "-",
                        str(int(o["right"])),
                        o["door"] or "-",
                        str(int(o["rev"])),
                        "%.2f" % o["up"],
                        "%.2f" % o["vp"],
                        str(int(o["trig"])),
                        bs,
                    ]
                )
            )
            print(lines[-1], flush=True)
    open(outp, "w").write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2:])
