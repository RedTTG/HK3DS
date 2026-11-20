#!/usr/bin/env python3
"""Rewrite the music / ambience / map zone fields of already-built .room files (build_room.area_of), without rebaking.
   python patch_room_audio.py <dir with Room.room files> [rooms...]"""
import sys, os, glob, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_room as BR
d = sys.argv[1]
rooms = sys.argv[2:] or [os.path.basename(p)[:-5] for p in glob.glob(f'{d}/*.room')]
for r in rooms:
    p = f'{d}/{r}.room'
    if not os.path.exists(p): continue
    b = bytearray(open(p, 'rb').read())
    if b[:4] != b'HKRM': print('skip (packed?)', r); continue
    m, a, z = BR.area_of(r)
    for i, s in enumerate((m, a, z)):
        b[20 + 32 * i: 52 + 32 * i] = struct.pack('32s', s.encode()[:31])
    open(p, 'wb').write(bytes(b))
    print(r, m, a, z)
