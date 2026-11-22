#!/usr/bin/env python3
"""Indexes of the AssetRipper export that the converters read (run once after exporting; HKDATA = the folder holding
the export's ex/):
  metamaps.pkl     (sprite guid -> 'Sprite/<name>.asset', texture guid -> 'Texture2D/<name>.png')
  tex2sprites.pkl  texture guid -> [(sprite name, [x, y, w, h] rect in the texture, sprite guid), ...]
  meta_AudioClip.pkl  audio clip guid -> the clip's file (build_music.py)
(uanim.py and match_textures.py / alpha_cache.py build their own caches the first time they run.)
   python index_export.py [out_dir]       (default: HKDATA)"""
import os, re, sys, glob, pickle

ROOT = os.environ.get('HKDATA', 'hkdata')
EX = f'{ROOT}/ex'
if not os.path.exists(ROOT):
    raise Exception(f'HKData not found at {ROOT}')

def guid_of(meta):
    m = re.search(r'^guid: ([0-9a-f]+)', open(meta, errors='replace').read(4000), re.M)
    return m.group(1) if m else None


def main(out):
    spr, tex, t2s = {}, {}, {}
    for meta in glob.glob(f'{EX}/Texture2D/*.png.meta'):
        g = guid_of(meta)
        if g: tex[g] = os.path.relpath(meta[:-5], EX)
    for meta in glob.glob(f'{EX}/Sprite/*.asset.meta'):
        g = guid_of(meta)
        if not g: continue
        path = meta[:-5]
        spr[g] = os.path.relpath(path, EX)
        s = open(path, errors='replace').read()
        name = re.search(r'^\s*m_Name:[ \t]*(.*)$', s, re.M)
        rect = re.search(r'm_Rect:\s*\n\s*serializedVersion: \d+\s*\n\s*x: ([-0-9.e]+)\s*\n\s*y: ([-0-9.e]+)\s*\n\s*width: ([-0-9.e]+)\s*\n\s*height: ([-0-9.e]+)', s)
        tg = re.search(r'^\s*texture: \{fileID: \d+, guid: ([0-9a-f]+)', s, re.M)
        if name and tg:
            t2s.setdefault(tg.group(1), []).append((name.group(1).strip() or '?', [float(v) for v in rect.groups()] if rect else None, g))
    audio = {}
    for meta in glob.glob(f'{EX}/AudioClip/*.meta'):
        g = guid_of(meta)
        if g: audio[g] = os.path.abspath(meta[:-5])
    pickle.dump(audio, open(f'{out}/meta_AudioClip.pkl', 'wb'))
    pickle.dump((spr, tex), open(f'{out}/metamaps.pkl', 'wb'))
    pickle.dump(t2s, open(f'{out}/tex2sprites.pkl', 'wb'))
    print(len(spr), 'sprites,', len(tex), 'textures,', len(t2s), 'textures with sprites,', len(audio), 'audio clips')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else ROOT)
