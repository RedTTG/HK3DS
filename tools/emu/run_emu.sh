#!/bin/bash
# run_emu.sh <outdir> <script> <shots>   — runs hk3ds.3dsx in the Azahar core, converts shots to PNG
out=$1; scr=$2; shots=$3
rm -rf $out; mkdir -p $out
rm -rf ${HK_EMU:-.}/run/saves/Azahar/sdmc/3ds/hk3ds 2>/dev/null
if [ -n "$HK_SAVE" ]; then mkdir -p ${HK_EMU:-.}/run/saves/Azahar/sdmc/3ds/hk3ds; printf "$HK_SAVE" > ${HK_EMU:-.}/run/saves/Azahar/sdmc/3ds/hk3ds/save.dat; fi
HK_SCRIPT="$scr" HK_SHOTS="$shots" ${HK_EMU:-.}/hkfront ${AZAHAR_CORE:?set AZAHAR_CORE to azahar_libretro.so} ${HK3DS:-$(cd "$(dirname "$0")/../.." && pwd)}/hk3ds.3dsx $out 2>$out/log.txt
python3 - "$out" <<'PY'
import sys, glob
from PIL import Image
for f in sorted(glob.glob(sys.argv[1] + '/*.ppm')):
    Image.open(f).save(f[:-4] + '.png')
PY
rm -f $out/*.ppm; ls $out
