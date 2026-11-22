#!/usr/bin/env python3
"""SceneManager.darknessLevel of every scene -> romfs/text/darkness.txt ("<scene> <level>", nonzero only).
python darkness.py <romfs> <scene list>"""

import sys, os, re

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_room as BR

out = []
for r in open(sys.argv[2]).read().split():
    p = BR.scene_path(r)
    if not p:
        continue
    m = re.search(r"^  darknessLevel: (-?\d+)", open(p, errors="ignore").read(), re.M)
    if m and int(m.group(1)) != 0:
        out.append(f"{r} {m.group(1)}")
open(f"{sys.argv[1]}/text/darkness.txt", "w").write("\n".join(out) + "\n")
print(len(out), "dark scenes")
