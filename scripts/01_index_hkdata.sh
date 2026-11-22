#!/bin/bash
# Clear and rebuild the HKDATA indexes and generated lookup data.
#   scripts/01_index_hkdata.sh [--hkdata DIR]
set -euo pipefail

H=$(cd "$(dirname "$0")/.." && pwd)
T=$H/tools
HKDATA=${HKDATA:-$H/hkdata}
cd "$H"

while [ "$#" -gt 0 ]; do
  case "$1" in
    --hkdata) HKDATA=$(python3 -c 'import os,sys; print(os.path.abspath(sys.argv[1]))' "${2:?usage: 01_index_hkdata.sh [--hkdata DIR]}"); shift 2 ;;
    --hkdata=*) HKDATA=$(python3 -c 'import os,sys; print(os.path.abspath(sys.argv[1]))' "${1#*=}"); shift ;;
    *) echo "usage: 01_index_hkdata.sh [--hkdata DIR]" >&2; exit 2 ;;
  esac
done

if [ ! -d "$HKDATA/ex" ]; then
  echo "HKDATA export not found at $HKDATA/ex" >&2
  exit 1
fi

echo "== clear indexes"
find "$HKDATA" -type f -name '*.pkl' -delete

echo "== clear generated data"
rm -f \
  "$T/data/audio_map.json" \
  "$T/data/script_guids.json" \
  "$T/data/cs_sigs.json" \
  "$T/data/fsm_templates.json"

echo "== index export"
HKDATA="$HKDATA" python3 "$T/index_export.py" "$HKDATA"

echo "== make audio map"
HKDATA="$HKDATA" python3 "$T/make_audio_map.py" "$HKDATA"

echo "== make script GUIDs"
HKDATA="$HKDATA" python3 "$T/make_script_guids.py" "$HKDATA"

echo "== make C# signatures"
HKDATA="$HKDATA" python3 "$T/cs_sigs.py"

echo "== make FSM templates"
HKDATA="$HKDATA" python3 "$T/fsm_templates.py" --quiet

echo "== make texture caches"
HKDATA="$HKDATA" python3 "$T/alpha_cache.py"
HKDATA="$HKDATA" python3 "$T/match_textures.py" --all

echo "== make animation cache"
HKDATA="$HKDATA" python3 - <<'PY'
import sys
sys.path.insert(0, "tools")
import uanim
uanim.index()
PY

echo "== make .meta caches"
HKDATA="$HKDATA" python3 - <<'PY'
import sys
sys.path.insert(0, "tools")
from hkcommon import metamap
for folder in ("AudioClip", "GameObject", "Material", "MonoBehaviour", "Sprite", "Texture2D"):
    metamap(folder)
PY

echo "DONE: HKDATA indexed"
