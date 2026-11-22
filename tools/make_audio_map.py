#!/usr/bin/env python3
"""Build tools/data/audio_map.json from an AssetRipper export."""

import argparse
import json
import os
import re
from pathlib import Path

MUSIC_CUE_SCRIPT = "400403b587ef0def1b40de34c0f1ebec"
GUID_RE = re.compile(r"^guid:\s*([0-9a-fA-F]{32})", re.MULTILINE)
GUID_REF_RE = re.compile(r"guid:\s*([0-9a-fA-F]{32})", re.IGNORECASE)


def cue_name(name):
    return name.lower().replace(" ", "_")


def read_guid(meta_path):
    match = GUID_RE.search(meta_path.read_text(errors="replace")[:4096])
    return match.group(1).lower() if match else None


def audio_name(asset_path):
    name = asset_path.name
    if name.endswith(".ogg"):
        name = name[:-4]
    elif name.endswith(".wav"):
        name = name[:-4]
    elif name.endswith(".asset"):
        name = name[:-6]
    return cue_name(name)


def resolve_asset(ex_dir, meta_path):
    asset = Path(str(meta_path)[:-5])
    if not asset.exists():
        return None
    return asset


def make_map(hkdata):
    ex_dir = hkdata / "ex"
    if not ex_dir.is_dir():
        raise FileNotFoundError(f"AssetRipper export not found: {ex_dir}")

    guid_assets = {}
    audio_names = {}
    music = {}

    for meta_path in ex_dir.rglob("*.meta"):
        guid = read_guid(meta_path)
        if not guid:
            continue

        asset = resolve_asset(ex_dir, meta_path)
        if asset is None:
            continue

        guid_assets[guid] = asset
        try:
            relative = asset.relative_to(ex_dir)
        except ValueError:
            continue

        if relative.parts and relative.parts[0] == "AudioClip":
            audio_names[guid] = audio_name(asset)

        if relative.parts and relative.parts[0] == "MonoBehaviour":
            text = asset.read_text(errors="replace")
            if MUSIC_CUE_SCRIPT in text and "channelInfos" in text:
                music[guid] = cue_name(asset.stem)

    atmos = {}
    atmos.update(audio_names)

    # Some AtmosCue GUIDs point to MonoBehaviour assets instead of AudioClip
    # assets. Resolve those assets through their AudioClip GUID references.
    for guid, asset in guid_assets.items():
        try:
            relative = asset.relative_to(ex_dir)
        except ValueError:
            continue
        if not relative.parts or relative.parts[0] != "MonoBehaviour":
            continue

        text = asset.read_text(errors="replace")
        candidates = []
        for ref_guid in GUID_REF_RE.findall(text):
            ref_guid = ref_guid.lower()
            if ref_guid in audio_names and audio_names[ref_guid] not in candidates:
                candidates.append(audio_names[ref_guid])

        if len(candidates) == 1:
            atmos[guid] = candidates[0]

    return {"music": music, "atmos": atmos}


def main():
    parser = argparse.ArgumentParser(
        description="Create tools/data/audio_map.json from the HKDATA AssetRipper export."
    )
    parser.add_argument(
        "hkdata",
        nargs="?",
        default=os.environ.get("HKDATA", "hkdata"),
        help="HKDATA folder (default: $HKDATA or ./hkdata)",
    )
    parser.add_argument(
        "-o",
        "--output",
        default=None,
        help="output JSON path (default: tools/data/audio_map.json beside this script)",
    )
    args = parser.parse_args()

    hkdata = Path(args.hkdata).expanduser().resolve()
    result = make_map(hkdata)

    output = (
        Path(args.output).expanduser().resolve()
        if args.output
        else Path(__file__).resolve().parent / "data" / "audio_map.json"
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")

    print(
        f"Wrote {output}: "
        f"{len(result['music'])} music GUIDs, "
        f"{len(result['atmos'])} atmos GUIDs"
    )


if __name__ == "__main__":
    main()
