#!/usr/bin/env python3
"""Walls the Knight can't cling to: terrain colliders on objects with a NonSlider component.
   python nonslider.py <romfs> <scene list>  -> romfs/text/nonslider.txt ("<scene> x0 y0 x1 y1" per collider path segment box)"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_room as BR
import build_objs as BO

def main(romfs, lst):
    out = []
    for r in open(lst).read().split():
        p = BR.scene_path(r)
        if not p: continue
        try: sc = BR.X.Scene(BR.X.load_scene(p))
        except Exception as e: print('skip', r, e); continue
        ns = set()
        for k, (t, v) in sc.objs.items():
            if t == 'MonoBehaviour' and BO.classify_mb(v) == 'NonSlider': ns.add(v['m_GameObject']['fileID'])
        if not ns: continue
        try: cols = BR.X.colliders(sc)
        except Exception as e:
            import traceback; traceback.print_exc(); print('skip', r); continue
        for c in cols:
            if c['gid'] not in ns or c['trigger'] or not c['active']: continue
            for pth in c['paths'] or []:
                n = len(pth)
                for i in range(n if c['type'] != 'EdgeCollider2D' else n - 1):
                    (x0, y0), (x1, y1) = pth[i], pth[(i + 1) % n]
                    if abs(x1 - x0) < 0.3:          # wall segments
                        out.append(f'{r} {min(x0,x1)-0.05:.2f} {min(y0,y1):.2f} {max(x0,x1)+0.05:.2f} {max(y0,y1):.2f}')
    open(f'{romfs}/text/nonslider.txt', 'w').write('\n'.join(out) + '\n')
    print(len(out), 'non-slider wall segments')

if __name__ == '__main__': main(sys.argv[1], sys.argv[2])
