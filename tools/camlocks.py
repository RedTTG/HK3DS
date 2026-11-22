#!/usr/bin/env python3
"""Export a scene's CameraLockArea zones for the 3DS camera (source/camera.c).
   python camlocks.py <romfs/rooms> Room [Room ...]
Writes <Room>.cam, one zone per line:
   L x0 y0 x1 y1 cameraXMin cameraXMax cameraYMin cameraYMax preventLookUp preventLookDown maxPriority
(x0..y1: the zone collider's world bounds; the camera* values are camera-centre limits, -1 = none).
"""

import sys, os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_room as BR

X = BR.X


def zones(scene_path):
    objs = X.load_scene(scene_path)
    sc = X.Scene(objs)
    cols = {}
    for c in X.colliders(sc):
        if c["active"]:
            cols.setdefault(c["gid"], c)
    out = []
    for t, v in objs.values():
        if t != "MonoBehaviour" or "cameraXMin" not in v or "cameraYMax" not in v:
            continue
        if not int(v.get("m_Enabled", 1) or 0):
            continue
        gid = v["m_GameObject"]["fileID"]
        c = cols.get(gid)
        if not c or gid not in sc.go2tr or not sc.world(sc.go2tr[gid])["active"]:
            continue
        b = c["box"]
        z = (
            b[0],
            b[1],
            b[2],
            b[3],
            float(v["cameraXMin"]),
            float(v["cameraXMax"]),
            float(v["cameraYMin"]),
            float(v["cameraYMax"]),
            int(v.get("preventLookUp") or 0),
            int(v.get("preventLookDown") or 0),
            int(v.get("maxPriority") or 0),
        )
        # a polygon zone (often L-shaped or slanted) keeps its outline: its bounding box would overlap its neighbours
        if c["type"] == "PolygonCollider2D" and c["paths"] and len(c["paths"][0]) >= 3:
            pts = c["paths"][0][:32]
            z += ("P", len(pts)) + tuple(float(q) for pt in pts for q in pt)
        out.append(z)
    return out


def export(room, out_dir):
    scene = BR.scene_path(room)
    if not scene:
        print(room, "no scene")
        return 0
    lock_zones = zones(scene)
    boss = BR.scene_path(room + "_boss")
    if boss:
        lock_zones += zones(boss)
    with open(f"{out_dir}/{room}.cam", "w") as handle:
        for zone in lock_zones:
            handle.write(
                "L "
                + " ".join(
                    "%.3f" % value if isinstance(value, float) else str(value)
                    for value in zone
                )
                + "\n"
            )
    print(room, len(lock_zones), "lock zones")
    return len(lock_zones)


if __name__ == "__main__":
    for room in sys.argv[2:]:
        export(room, sys.argv[1])
