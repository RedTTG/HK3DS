#!/usr/bin/env python3
"""A patch over the installed CIAs, so players don't download the whole game again:
  luma/titles/<title id>/code.bin + exheader.bin   the new game code (Luma3DS "Enable game patching")
  3ds/hk3ds/patch/<std|hd>/...                     data files changed since the CIA release, and files.txt
Patches are cumulative: each one holds everything changed since the base CIAs, so only the newest is needed.
One zip per edition (standard, HD), each with only its own title.

   python tools/make_patch.py BASE_DIR OUT_DIR
BASE_DIR holds std.txt and hd.txt, the file lists of the released CIAs (tools/ctrfile.py romfs X.cia list.txt).
Build hk3ds.elf (make) and hk3ds_hd.elf (make HD=1) first; the version comes from source/version.h."""
import hashlib, os, re, shutil, subprocess, sys, tempfile, zipfile

HERE = os.path.dirname(os.path.abspath(__file__)); TOP = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import ctrfile

EDITIONS = (('std', 'hk3ds', 'cia/hk3ds.rsf', 'romfs'), ('hd', 'hk3ds_hd', 'cia/hk3ds_hd.rsf', 'romfs_hd'))
SKIP = {'text/version.txt'}      # describes the CIA's own data
BASE_CHECK = ('rooms/Town.lyr', 'rooms/Tutorial_01.lyr', 'rooms/Crossroads_02.lyr', 'gfx/areas.spr')


def version():
    return re.search(r'HK3DS_VERSION "([^"]+)"', open(f'{TOP}/source/version.h').read()).group(1)


def build_code(target, rsf, tmp):
    """code.bin + exheader.bin of the current ELF, made with the CIA's rsf (a dummy romfs: the exheader doesn't
    depend on it)"""
    dummy = f'{tmp}/romfs_{target}'; os.makedirs(dummy, exist_ok=True)
    open(f'{dummy}/x', 'w').write('x')
    r = re.sub(r'(?m)^(\s*RootPath\s*:\s*).*$', lambda m: m.group(1) + dummy, open(f'{TOP}/{rsf}').read())
    open(f'{tmp}/{target}.rsf', 'w').write(r)
    cxi = f'{tmp}/{target}.cxi'
    subprocess.run(['makerom', '-f', 'cxi', '-o', cxi, '-elf', f'{TOP}/{target}.elf', '-rsf', f'{tmp}/{target}.rsf',
                    '-exefslogo', '-target', 't'], check=True, cwd=TOP)
    n = ctrfile.NCCH(cxi)
    return n.tid, n.code(), n.exheader()


def check_code(code, elf):
    """the code.bin must be the ELF's segments, laid out as makerom does"""
    out = subprocess.run([os.environ.get('READELF', '/opt/devkitpro/devkitARM/bin/arm-none-eabi-readelf'), '-lW', elf],
                         capture_output=True, text=True, check=True).stdout
    raw = open(elf, 'rb').read(); base = None
    for l in out.splitlines():
        p = l.split()
        if p and p[0] == 'LOAD':
            off, va, _, fsz = (int(x, 16) for x in p[1:5])
            base = va if base is None else base
            if code[va - base:va - base + fsz] != raw[off:off + fsz]: sys.exit(f'code.bin does not match {elf}')


def changed_files(base_list, root):
    base = {}
    for l in open(base_list):
        h, s, p = l.rstrip('\n').split(' ', 2); base[p] = h
    out, removed = [], [p for p in base if not os.path.exists(f'{root}/{p}')]
    for d, _, fs in os.walk(root):
        for f in fs:
            p = os.path.relpath(os.path.join(d, f), root)
            if p in SKIP: continue
            if base.get(p) != hashlib.sha1(open(os.path.join(root, p), 'rb').read()).hexdigest(): out.append(p)
    return sorted(out), removed


README = """HK3DS {edition} patch v{ver}
{line}
This updates an installed HK3DS {edition} v{base} ({cia}) to v{ver} without reinstalling the CIA.
It is only for the {edition} version: the other version has its own patch zip.

1. Copy the "luma" and "3ds" folders to the root of your SD card (merge them with the folders already there).
2. Turn on game patching: hold SELECT while powering on to open the Luma3DS menu, select
   "Enable game patching", then START to save.
3. Start "{title}": the title screen shows v{ver}.

Each patch holds everything since v{base}: a newer patch replaces this one.
When you later install a full CIA of a newer version, delete SD:/luma/titles/{tid}
(the game warns on its title screen if an old patch is still loaded).

Changed data files: {n}.
"""
NAMES = {'std': ('standard', 'hk3ds.cia', 'Hollow Knight 3DS'), 'hd': ('HD', 'hk3ds_hd.cia', 'Hollow Knight 3DS HD')}


def main(base_dir, out_dir):
    """one zip per edition (hk3ds_patch_v<ver>_std.zip, _hd.zip): each holds only its own title's code and data"""
    ver = version(); base_ver = open(f'{base_dir}/version.txt').read().strip() if os.path.exists(f'{base_dir}/version.txt') else '?'
    with tempfile.TemporaryDirectory() as tmp:
        for ed, target, rsf, root in EDITIONS:
            stage = f'{out_dir}/hk3ds_patch_v{ver}_{ed}'
            shutil.rmtree(stage, ignore_errors=True); os.makedirs(stage)
            tid, code, exh = build_code(target, rsf, tmp)
            check_code(code, f'{TOP}/{target}.elf')
            t = f'{stage}/luma/titles/{tid:016X}'; os.makedirs(t)
            open(f'{t}/code.bin', 'wb').write(code); open(f'{t}/exheader.bin', 'wb').write(exh)
            files, removed = changed_files(f'{base_dir}/{ed}.txt', f'{TOP}/{root}')
            if removed: print(f'warning: {ed}: {len(removed)} files of the CIA no longer exist (the CIA still has them):', removed[:5])
            pd = f'{stage}/3ds/hk3ds/patch/{ed}'; os.makedirs(pd)
            for p in files:
                os.makedirs(os.path.dirname(f'{pd}/{p}'), exist_ok=True); shutil.copyfile(f'{TOP}/{root}/{p}', f'{pd}/{p}')
            # a few room-art files of the base CIA with their sizes: the game checks them and warns when another
            # version's data is installed (the code would read room art in the wrong format)
            sizes = {l.split()[2]: int(l.split()[1]) for l in open(f'{base_dir}/{ed}.txt') if len(l.split()) == 3}
            base_lines = ''.join(f'BASE {p} {sizes[p]}\n' for p in BASE_CHECK if p in sizes)
            open(f'{pd}/files.txt', 'w').write(f'HKPATCH {ver}\n' + base_lines + ''.join(p + '\n' for p in files))
            edition, cia, title = NAMES[ed]
            head = f'HK3DS {edition} patch v{ver}'
            open(f'{stage}/README_PATCH.txt', 'w').write(README.format(edition=edition, line='=' * len(head), ver=ver, base=base_ver,
                                                                     cia=cia, title=title, tid=f'{tid:016X}', n=len(files)))
            z = f'{out_dir}/hk3ds_patch_v{ver}_{ed}.zip'
            with zipfile.ZipFile(z, 'w', zipfile.ZIP_DEFLATED) as zf:
                for d, _, fs in os.walk(stage):
                    for f in fs:
                        p = os.path.join(d, f); zf.write(p, os.path.relpath(p, stage))
            print(f'{ed}: title {tid:016X}, code.bin {len(code)} B, {len(files)} data files -> {z} ({os.path.getsize(z)} bytes)')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
