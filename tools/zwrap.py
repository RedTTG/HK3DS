# Wrap data files in zlib ('HKZ1' u32 rawSize + zlib stream), in place. Already-wrapped files are skipped.
import sys, zlib, struct, os

tot0 = tot1 = 0
for p in sys.argv[1:]:
    d = open(p, "rb").read()
    if d[:4] == b"HKZ1":
        continue
    z = b"HKZ1" + struct.pack("<I", len(d)) + zlib.compress(d, 9)
    tot0 += len(d)
    tot1 += len(z)
    open(p, "wb").write(z)
print(f"{tot0 // 1024} KB -> {tot1 // 1024} KB")
