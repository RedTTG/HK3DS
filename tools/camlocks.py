#!/usr/bin/env python3
"""Export a scene's CameraLockArea zones for the 3DS camera (source/camera.c).
   python camlocks.py <romfs/rooms> Room [Room ...]
Writes <Room>.cam, one zone per line:
   L x0 y0 x1 y1 cameraXMin cameraXMax cameraYMin cameraYMax preventLookUp preventLookDown maxPriority
(x0..y1: the zone collider's world bounds; the camera* values are camera-centre limits, -1 = none)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_room as BR
X = BR.X

def zones(scene_path):
    objs = X.load_scene(scene_path)
    sc = X.Scene(objs)
    cols = {}
    for c in X.colliders(sc):
        if c['active']: cols.setdefault(c['gid'], c)
    out = []
    for t, v in objs.values():
        if t != 'MonoBehaviour' or 'cameraXMin' not in v or 'cameraYMax' not in v: continue
        if not int(v.get('m_Enabled', 1) or 0): continue
        gid = v['m_GameObject']['fileID']
        c = cols.get(gid)
        if not c or gid not in sc.go2tr or not sc.world(sc.go2tr[gid])['active']: continue
        b = c['box']
        z = (b[0], b[1], b[2], b[3], float(v['cameraXMin']), float(v['cameraXMax']), float(v['cameraYMin']),
             float(v['cameraYMax']), int(v.get('preventLookUp') or 0), int(v.get('preventLookDown') or 0),
             int(v.get('maxPriority') or 0))
        # a polygon zone (often L-shaped or slanted) keeps its outline: its bounding box would overlap its neighbours
        if c['type'] == 'PolygonCollider2D' and c['paths'] and len(c['paths'][0]) >= 3:
            pts = c['paths'][0][:32]
            z += ('P', len(pts)) + tuple(float(q) for pt in pts for q in pt)
        out.append(z)
    return out

if __name__ == '__main__':
    out_dir = sys.argv[1]
    for room in sys.argv[2:]:
        p = BR.scene_path(room)
        if not p: print(room, 'no scene'); continue
        zs = zones(p)
        bp = BR.scene_path(room + '_boss')
        if bp: zs += zones(bp)
        with open(f'{out_dir}/{room}.cam', 'w') as fp:
            for z in zs: fp.write('L ' + ' '.join('%.3f' % x if isinstance(x, float) else str(x) for x in z) + '\n')
        print(room, len(zs), 'lock zones')
