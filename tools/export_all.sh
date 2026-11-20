#!/bin/bash
# Re-export and pack the live objects (enemies, NPCs, interactables) of every room.
#   tools/export_all.sh <objdir> <romfs> <room list file>
OBJ=$1; ROMFS=$2; LIST=$3
cd "$(dirname "$0")"
mkdir -p "$OBJ"
for r in $(cat "$LIST"); do
  nice python3 build_objs.py "$OBJ" "$r" 2>&1 | tail -1
done
for r in $(cat "$LIST"); do
  nice python3 pack_objs.py "$OBJ" "$ROMFS" "$r" 2>&1 | grep -v "^  " | tail -1
  [ -f "$ROMFS/rooms/${r}_objs.spr" ] && python3 zwrap.py "$ROMFS/rooms/${r}_objs.spr" >/dev/null
done
echo ALLDONE
