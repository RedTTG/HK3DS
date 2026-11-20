#!/bin/bash
# Rebuild every room from the game data: live objects first (their roots are left out of the baked art), then the
# room art / collision, then install into romfs and pack the objects.
#   tools/full_rebuild.sh <objdir> <build dir> <romfs> <room list file>
#   optional 5th argument: the phases to run (default "objects rooms install pack")
OBJ=$1; FB=$2; ROMFS=$3; LIST=$4; PH=${5:-"objects rooms install pack"}
cd "$(dirname "$0")"
mkdir -p "$OBJ" "$FB"
case "$PH" in *objects*) echo "== objects"
for r in $(cat "$LIST"); do nice python3 build_objs.py "$OBJ" "$r" 2>&1 | tail -1; done ;; esac
case "$PH" in *rooms*) echo "== rooms"
for r in $(cat "$LIST"); do nice python3 build_room.py "$FB" "$r" 2>&1 | grep -v "^  \|top:\|matched" | tail -1; done ;; esac
case "$PH" in *install*) echo "== install"
./install_rooms.sh "$FB" "$ROMFS" $(cat "$LIST") 2>&1 | grep -v "lock zones\|mask groups" | tail -3
# rooms built one at a time only link exits to rooms in the same batch: link every exit to the installed rooms
python3 fill_gate_targets.py "$ROMFS/rooms" "$FB" | tail -1 ;; esac
case "$PH" in *pack*) echo "== pack"
for r in $(cat "$LIST"); do
  nice python3 pack_objs.py "$OBJ" "$ROMFS" "$r" 2>&1 | grep -v "^  " | tail -1
  [ -f "$ROMFS/rooms/${r}_objs.spr" ] && python3 zwrap.py "$ROMFS/rooms/${r}_objs.spr" >/dev/null
done ;; esac
echo ALLDONE
