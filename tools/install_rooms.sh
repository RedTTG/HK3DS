#!/bin/bash
# Copy freshly built rooms (build_room.py output) into romfs and finish them: HKL2 layers, zlib wrapping, camera locks,
# secret masks.   tools/install_rooms.sh <build dir> <romfs> Room [Room ...]
B=$1; R=$2; shift 2
cd "$(dirname "$0")"
for r in "$@"; do
  [ -f "$B/$r.room" ] && python3 patch_room_audio.py "$B" "$r" >/dev/null
  for ext in .room .lyr _ents.spr _enemies.spr; do
    [ -f "$B/$r$ext" ] && cp "$B/$r$ext" "$R/rooms/"
  done
  [ -f "$R/rooms/$r.lyr" ] && python3 lyr_v2.py "$R/rooms/$r.lyr" >/dev/null
  for f in "$R/rooms/$r.room" "$R/rooms/$r.lyr" "$R/rooms/${r}_ents.spr" "$R/rooms/${r}_enemies.spr"; do [ -f "$f" ] && python3 zwrap.py "$f" >/dev/null; done
  nice python3 camlocks.py "$R/rooms" "$r" 2>&1 | tail -1
  nice python3 masks.py "$R/rooms" "$r" 2>&1 | tail -1
  [ -f "$R/rooms/${r}_msk.spr" ] && python3 zwrap.py "$R/rooms/${r}_msk.spr" >/dev/null
  echo "Done with $r"
done
# a room built on its own only links exits to rooms built with it: relink every exit to the installed rooms
python3 fill_gate_targets.py "$R/rooms" "$B" | tail -1
# exits the game hides by PlayerData (the Grimm Troupe tents...) only work while their object would be shown
python3 patch_gate_conditions.py "$R/rooms" "$@" | tail -1
