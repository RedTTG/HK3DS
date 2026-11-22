# HK3DS: Hollow Knight on the Nintendo 3DS

A fan port of Hollow Knight to the (New) Nintendo 3DS and 2DS family. The game's own rooms, art, music, scripts and
state machines are converted from **your own copy of Hollow Knight** and run on a 3DS engine written for this project.

> **No game files are included.** This repository contains only the port's code and the tools that convert your copy
> of the game. Hollow Knight and all of its assets belong to Team Cherry; buy the game to use this. The project is a
> personal, non-commercial fan work, not affiliated with or endorsed by Team Cherry. Don't upload converted assets,
> `romfs/` folders or built `.3dsx` / `.cia` files.

<!-- Showcase video: open this README in GitHub's editor and drag hk3ds_showcase.mp4 here to embed it. -->

## Two versions

| | Standard | HD |
|---|---|---|
| Consoles | every 3DS and 2DS | New 3DS, New 3DS XL, New 2DS XL |
| Top screen | 400x240 | 800x240 (the New 3DS's wide mode) |
| Art | room art, sprites, fonts and HUD at 3DS resolution | everything converted again with twice the detail across |
| Title | Hollow Knight 3DS | Hollow Knight 3DS HD |

Both install side by side and share the same save files (`SD:/3ds/hk3ds/`).

## What's in
- **The whole map**: King's Pass to Godhome, 480 scenes, each converted from the game's own scene.
- **The game's own logic**: rooms run the game's PlayMaker state machines and ported components, so enemies, bosses,
  NPCs, doors, levers, lifts and trams behave as they do in the game. The Knight runs on his own state machines too.
- **NPCs, shops and dialogue**: Elderbug, Cornifer and his maps, Iselda, Sly, Salubra, the Nailsmith, the Stags, the
  Grubfather, the Seer and the rest, with their voices and conversation scripts.
- **All 40 charms**, every spell and ability, nail arts, the Dream Nail and essence.
- **Bosses and Godhome**: arenas load the way the game does; the Hall of Gods and all five pantheons.
- **The Hunter's Journal**, the inventory, the map on the bottom screen (it fills in at benches with the Quill).
- **Endings** with their titles and the credits.
- **Music and sound**: each room's music cues and ambience, the game's sound effects.
- **English and Spanish**, and Options for Button Hints, sound and more.

Controls: **B** jump · **Y** nail (Up/Down to aim, hold for nail arts) · **A** focus (hold) / cast (tap) · **R** dash ·
**X** dream nail · **ZR** quick cast · **ZL** super dash · **L** quick map · **SELECT** inventory · **START** pause ·
**Up** talk / inspect / enter doors · Circle Pad or D-Pad to move.

## Building it

You need:
- **Hollow Knight** (PC), exported with [AssetRipper](https://github.com/AssetRipper/AssetRipper) (see below).
- **devkitPro** with `3ds-dev` (`libctru`, `citro2d`, `citro3d`); `bannertool` and `makerom` for CIAs.
- **Python 3** with `numpy`, `Pillow` and `PyYAML`, and **ffmpeg** (sounds and music).

### 1. Export the game
Put these in one folder and point `HKDATA` at it (`export HKDATA=/path/to/hkdata`):
```
hkdata/
  ex/                  AssetRipper's export of the game as a Unity project (Assets/ contents:
                       Scenes, GameObject, Sprite, Texture2D, AudioClip, AnimationClip, MonoBehaviour, ...)
  hs/Texture2D/        every Texture2D of the game dumped as PNG (any names); used to find atlases the
                       export didn't link to their sprites
  Assembly-CSharp/     the game's decompiled scripts (AssetRipper's script export)
```
Then build the indexes the converters read:
```
python tools/index_export.py        # metamaps.pkl, tex2sprites.pkl, meta_AudioClip.pkl in $HKDATA
python tools/alpha_cache.py         # hs_alpha4.pkl
python tools/match_textures.py --all
```

`tools/data/` holds the port's own lookup tables, which the converters read: `audio_map.json` (music and ambience
cues), `script_guids.json` and `cs_sigs.json` (which game script each component is), `fsm_templates.json`,
`particle_textures.json` and `uoroots/` (the objects each room converts). They hold no game assets.
`cs_sigs.json` and `fsm_templates.json` can be rebuilt from your export with `python tools/cs_sigs.py` and
`python tools/fsm_templates.py`.

### 2. Convert the game into `romfs/`
Run `scripts/01_index_hkdata.sh [--hkdata hkdata]` once after exporting HKDATA. It deletes every generated `.pkl` under
HKDATA, clears the generated JSON lookup maps, and rebuilds the export, audio, C# signature, script GUID, FSM template,
texture, animation, and `.meta` indexes.

`scripts/03_full_rebuild.sh [--build build]` runs the whole room conversion. The build directory defaults to `build/`; objects,
room build outputs and the ROM filesystem are stored at `build/objs/`, `build/`, and `build/romfs/`. The room list comes
from `tools/data/all_rooms.txt`. Its steps are:
1. `python tools/build_objs.py <objdir> <Room...>`: every live object in a scene (NPCs, enemies, interactables, their
   PlayMaker FSMs, components, the prefabs they spawn, sounds).
2. `python tools/build_room.py <out_dir> <Room>`: collision, transitions, entities and the baked parallax art layers.
3. `scripts/04_install_rooms.sh [--build build] [Room...]` copies and compresses the room files and exports the secret
   masks (`tools/masks.py`); `python tools/pack_objs.py <objdir> romfs <Room...>` writes `<Room>.obj` and
   `<Room>_objs.spr` and converts the sounds; then `python tools/obj_particles.py romfs <Room...>` adds the objects'
   particle systems and sprite materials to that `<Room>.obj` (run it again after every `pack_objs.py`: enemy shots
   such as the Fungoons' gas are only particles).
4. Game-wide data: `tools/build_knight.py` (the Knight's state machines), `build_sprites.py`, `build_ui.py`,
   `build_map.py`, `build_music.py`, `build_journal.py`, `text_sheets.py`, `darkness.py`, `pantheons.py`,
   `gg_doors.py`, `build_vignette.py`.

The `romfs/` files are zlib-wrapped (`HKZ1` header, `tools/zwrap.py`) and unpacked on the 3DS with tinf.

**HD (`romfs_hd/`)** starts as a hard-linked copy of `romfs/` (`cp -al romfs romfs_hd`); the HD steps replace files in
it with twice-the-detail versions in the same formats:
`HK_XD=2 python tools/rebake_layers.py` (room art), `tools/pack_objs_hd.py` (object sprites),
`HK_XD=2 python tools/build_sprites.py` (Knight and enemies), `tools/build_ui_hd.py` (fonts, HUD, title screen).

### 3. Build
```
make            # hk3ds.3dsx (romfs/ embedded)
make cia        # hk3ds.cia
make HD=1 cia   # hk3ds_hd.cia (from romfs_hd/, builds in build_hd/)
```
The CIA needs `icon.png` (48x48) and `cia/banner.png` (256x128) + `cia/banner.wav`; make your own.

## Installing
Install the CIA with **FBI** (or over Wi-Fi: `python install/install_with_fbi.py` shows a QR code for FBI's Remote
Install). Sound needs `SD:/3ds/dspfirm.cdc` (dump it with DSP1).

The HD build needs the New 3DS's full 124 MB. If line 2 of `SD:/3ds/hk3ds/log.txt` says `low memory`, turn off
Luma3DS's "New 3DS CPU" setting (hold SELECT at boot): both builds already run at 804 MHz with the L2 cache.

## Updating without reinstalling
From v1.0.22, updates come as a small patch zip over the installed CIA. From v1.0.23 there is one zip per version
(`hk3ds_patch_v<version>_std.zip` for the standard CIA, `_hd.zip` for the HD one): use the one for the CIA you have.
1. Copy the zip's `luma` and `3ds` folders to the root of the SD card.
2. Turn on Luma3DS's **Enable game patching** (hold SELECT while powering on, select it, START to save).
3. The title screen shows the new version.

The new code goes in through Luma's game patching (`SD:/luma/titles/<title id>/code.bin` and `exheader.bin`;
standard `000400000F4C3D00`, HD `000400000F4C3E00`). Data files changed since the CIA release go in
`SD:/3ds/hk3ds/patch/std|hd/`, and the game reads them from there instead of the CIA (`source/patch.c`). Patches are
cumulative, and a patch's data is only used by the version it was made for. If you install a newer full CIA later,
delete the title's folder in `SD:/luma/titles`; the title screen warns you if an old patch is still loaded.

`tools/make_patch.py` builds a patch from your two ELFs and the file lists of the CIAs you released
(`tools/ctrfile.py romfs X.cia list.txt`). It checks the code against the ELF, because Luma halts the console on a
malformed `code.bin`.

## Testing without a 3DS
`host/` is a small software stand-in for libctru and citro2d that runs the real game code on a PC, headless, with
scripted input and screenshots:
```
gcc -O2 -w -Ihost -Isource -D__HOST__ source/*.c host/host.c -lm -lpthread -o host/hk3ds_host   # add -DHK_HD for HD
HK_SCRIPT="150:;1:A;40:;1:A;200:RIGHT" HK_SHOTS="300" ./host/hk3ds_host     # -> shots/f00300.png
HK_START="Crossroads_10 -1 -1" ./host/hk3ds_host                             # start a new game in any room
```
`scripts/05_regress.sh [--build build]` runs the regression scenes. `tools/emu/` has a libretro front end (`hkrec.c`) that runs the real
`.3dsx` in the Azahar core with scripted input and records video and sound (`HK_REC_LEN` starts when a room fades in).
Azahar's software renderer shows only half of the New 3DS's 800-pixel wide mode; `tools/emu/azahar-wide-mode.patch`
fixes that for HD recordings.

## Layout
```
source/    the game (C, libctru + citro2d); source/ext/ has the bundled libraries
host/      the PC stand-in for testing
tools/     the converters (Python) and test scripts
cia/       CIA metadata (.rsf)
install/   the Wi-Fi installer (bundles segno for the QR code)
```

## Licenses
The port's code and tools are under the MIT license (`LICENSE`). Bundled libraries keep their own licenses:
libdeflate (MIT), tinf (zlib), stb_vorbis and stb_image_write (public domain / MIT), segno (BSD-3-Clause).
Hollow Knight © Team Cherry. None of its assets are in this repository.

See `CHANGELOG.md` for the version history.
