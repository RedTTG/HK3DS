#!/usr/bin/env python3
"""Updates, so players don't download the whole game again. One zip per edition (standard, HD).
From v1.1 (the game data is a file on the SD card, SD:/3ds/hk3ds/hk3ds_<std|hd>.romfs):
  hk3ds.cia / hk3ds_hd.cia                         the new code: a small CIA, installed over the old one with FBI
  3ds/hk3ds/hk3ds.3dsx (standard)                  the same for the Homebrew Launcher
  3ds/hk3ds/patch/<std|hd>/...                     data files changed since the data file's release, and files.txt
Before v1.1 (the data inside the CIA) the code went in through Luma3DS's game patching instead:
  luma/titles/<title id>/code.bin + exheader.bin
Updates are cumulative: each one holds everything changed since the base release, so only the newest is needed.

   python tools/make_patch.py BASE_DIR OUT_DIR         an update over the release whose file lists are in BASE_DIR
   python tools/make_patch.py --record-base BASE_DIR   write BASE_DIR's lists from romfs/ and romfs_hd/ (at a release)
   python tools/make_patch.py --release OUT_DIR        a full release: the program zips beside the data files (make data),
                                                       and the lists in patches/base_v<version>/
BASE_DIR holds version.txt and std.txt / hd.txt ("sha1 size path" per data file).
Build the code first (make cia, make HD=1 cia from v1.1; the ELFs before); the version comes from source/version.h."""
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


def record_base(base_dir):
    """the file lists of the data being released (romfs/, romfs_hd/: what make data packs), for later updates"""
    os.makedirs(base_dir, exist_ok=True)
    for ed, target, rsf, root in EDITIONS:
        rows = []
        for d, _, fs in os.walk(f'{TOP}/{root}'):
            for f in fs:
                p = os.path.join(d, f); rel = os.path.relpath(p, f'{TOP}/{root}')
                b = open(p, 'rb').read(); rows.append(f'{hashlib.sha1(b).hexdigest()} {len(b)} {rel}')
        open(f'{base_dir}/{ed}.txt', 'w').write('\n'.join(sorted(rows, key=lambda r: r.split(' ', 2)[2])) + '\n')
        print(f'{ed}: {len(rows)} files')
    open(f'{base_dir}/version.txt', 'w').write(version() + '\n')


def is_v11(v):
    """a base release with its data on the SD card (v1.1 and later)"""
    try: return tuple(int(x) for x in v.split('.')[:2]) >= (1, 1)
    except ValueError: return False


UPDATE_README = """HK3DS {edition} update v{ver}
{line}
This updates HK3DS {edition} v{base} or later to v{ver}. It needs the game data file of v{base}
(SD:/3ds/hk3ds/{datafile}), which stays as it is: only the code and the files changed since then are here.

1. Copy the "3ds" folder to the root of your SD card (merge it with the one already there, replace files).
2. Install {cia} with FBI (it replaces the installed one; your saves stay).
   Using the Homebrew Launcher instead? Start 3ds/hk3ds/{tdsx} from it; no CIA needed.
3. The title screen shows v{ver}.

Each update holds everything since v{base}: a newer update replaces this one.
Changed data files: {n}.
"""
DATAFILES = {'std': 'hk3ds_std.romfs', 'hd': 'hk3ds_hd.romfs'}


def main_v11(base_dir, out_dir, ver, base_ver):
    """v1.1 and later: the small CIA (and .3dsx) with the changed data files"""
    for ed, target, rsf, root in EDITIONS:
        stage = f'{out_dir}/hk3ds_update_v{ver}_{ed}'
        shutil.rmtree(stage, ignore_errors=True); os.makedirs(f'{stage}/3ds/hk3ds')
        cia = f'{TOP}/{target}.cia'
        if not os.path.exists(cia) or os.path.getsize(cia) > 64 << 20:
            sys.exit(f'{target}.cia is missing or still holds the data: build it with make{" HD=1" if ed == "hd" else ""} cia')
        check_code(ctrfile.NCCH(cia).code(), f'{TOP}/{target}.elf')        # the CIA is this build's code
        shutil.copyfile(cia, f'{stage}/{target}.cia')
        shutil.copyfile(f'{TOP}/{target}.3dsx', f'{stage}/3ds/hk3ds/{target}.3dsx')
        files, removed = changed_files(f'{base_dir}/{ed}.txt', f'{TOP}/{root}')
        if removed: print(f'warning: {ed}: {len(removed)} files of the data file no longer exist (it still has them):', removed[:5])
        pd = f'{stage}/3ds/hk3ds/patch/{ed}'; os.makedirs(pd)
        for p in files:
            os.makedirs(os.path.dirname(f'{pd}/{p}'), exist_ok=True); shutil.copyfile(f'{TOP}/{root}/{p}', f'{pd}/{p}')
        sizes = {l.split()[2]: int(l.split()[1]) for l in open(f'{base_dir}/{ed}.txt') if len(l.split()) == 3}
        base_lines = ''.join(f'BASE {p} {sizes[p]}\n' for p in BASE_CHECK if p in sizes)
        open(f'{pd}/files.txt', 'w').write(f'HKPATCH {ver}\n' + base_lines + ''.join(p + '\n' for p in files))
        edition = NAMES[ed][0]; head = f'HK3DS {edition} update v{ver}'
        open(f'{stage}/README_UPDATE.txt', 'w').write(UPDATE_README.format(edition=edition, line='=' * len(head), ver=ver,
            base=base_ver, datafile=DATAFILES[ed], cia=f'{target}.cia', tdsx=f'{target}.3dsx', n=len(files)))
        z = f'{out_dir}/hk3ds_update_v{ver}_{ed}.zip'
        with zipfile.ZipFile(z, 'w', zipfile.ZIP_DEFLATED) as zf:
            for d, _, fs in os.walk(stage):
                for f in fs:
                    p = os.path.join(d, f); zf.write(p, os.path.relpath(p, stage))
        print(f'{ed}: {target}.cia, {len(files)} data files -> {z} ({os.path.getsize(z)} bytes)')


def main(base_dir, out_dir):
    """one zip per edition (hk3ds_patch_v<ver>_std.zip, _hd.zip): each holds only its own title's code and data"""
    ver = version(); base_ver = open(f'{base_dir}/version.txt').read().strip() if os.path.exists(f'{base_dir}/version.txt') else '?'
    if is_v11(base_ver): return main_v11(base_dir, out_dir, ver, base_ver)
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


RELEASE_README = """Hollow Knight 3DS{hd} v{ver}
{line}
A fan port, for personal use: not for sale. You need the game data file of this version too:
{datafile} ({size} MB), from the same release.

1. Copy {datafile} into SD:/3ds/hk3ds/ (create the folder if it isn't there).
2. Copy this zip's "3ds" folder to the root of your SD card, and install {cia} with FBI.
   It installs over an earlier version; your saves stay.{hbl}
3. Start the game: the title screen shows v{ver}.

Coming from v1.0.22 to v1.0.25 (the Luma patches)? Delete SD:/luma/titles/{tid}, or Luma keeps
running the old code, which can't find the new data file. SD:/3ds/hk3ds/patch/ can go too.
{newds}
Later updates are small zips (a new CIA and the files that changed); the data file stays.
"""


def release(out_dir):
    """a full release: per edition a program zip (CIA, .3dsx, README) beside its data file (make data), and the data's
    file lists in patches/base_v<version>/ for later updates"""
    ver = version(); os.makedirs(out_dir, exist_ok=True)
    for ed, target, rsf, root in EDITIONS:
        cia, data = f'{TOP}/{target}.cia', f'{TOP}/{DATAFILES[ed]}'
        if not os.path.exists(data): sys.exit(f'{DATAFILES[ed]} is missing: make{" HD=1" if ed == "hd" else ""} data')
        if not os.path.exists(cia) or os.path.getsize(cia) > 64 << 20: sys.exit(f'{target}.cia missing or holds the data')
        n = ctrfile.NCCH(cia); check_code(n.code(), f'{TOP}/{target}.elf')
        hd = ' HD' if ed == 'hd' else ''
        head = f'Hollow Knight 3DS{hd} v{ver}'
        txt = RELEASE_README.format(hd=hd, ver=ver, line='=' * len(head), datafile=DATAFILES[ed], cia=f'{target}.cia',
                                    size=os.path.getsize(data) >> 20, tid=f'{n.tid:016X}',
                                    hbl='' if hd else '\n   Or start 3ds/hk3ds/hk3ds.3dsx from the Homebrew Launcher instead of installing the CIA.',
                                    newds='This version needs a New 3DS, New 3DS XL or New 2DS XL.\n' if hd else '')
        z = f'{out_dir}/hk3ds_v{ver}_{ed}.zip'
        with zipfile.ZipFile(z, 'w', zipfile.ZIP_DEFLATED) as zf:
            zf.write(cia, f'{target}.cia'); zf.write(f'{TOP}/{target}.3dsx', f'3ds/hk3ds/{target}.3dsx')
            zf.writestr('README.txt', txt)
        print(f'{ed}: {z} ({os.path.getsize(z)} bytes) + {data} ({os.path.getsize(data) >> 20} MB)')
    record_base(f'{TOP}/patches/base_v{ver}')


if __name__ == '__main__':
    if sys.argv[1] == '--record-base': record_base(sys.argv[2])
    elif sys.argv[1] == '--release': release(sys.argv[2])
    else: main(sys.argv[1], sys.argv[2])
