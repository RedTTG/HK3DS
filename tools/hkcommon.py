# Shared helpers for reading the AssetRipper export of Hollow Knight.
import re, os, glob, pickle, math, functools
import yaml
Loader = getattr(yaml, "CSafeLoader", yaml.SafeLoader)
ROOT = os.environ.get('HKDATA', 'hkdata')
if not os.path.exists(ROOT):
    raise Exception(f'HKData not found at {ROOT}')
EX = f'{ROOT}/ex'
KNIGHT_ATLAS = os.environ.get('KNIGHT_ATLAS', '/mnt/user-data/uploads/Downloads/knight_atlas.png')

def load_yaml_docs(path):
    src = open(path, encoding='utf-8').read()
    parts = re.split(r'^--- !u!(\d+) &(-?\d+).*$', src, flags=re.M)
    objs = {}
    for i in range(1, len(parts), 3):
        try:
            d = yaml.load(parts[i + 2], Loader=Loader)
        except Exception:
            continue
        if d:
            (typ, val), = d.items()
            objs[int(parts[i + 1])] = (typ, val)
    return objs

@functools.lru_cache(None)
def metamap(folder):
    cache = f'{ROOT}/meta_{folder}.pkl'
    if os.path.exists(cache): return pickle.load(open(cache, 'rb'))
    g = {}
    for m in glob.glob(f'{EX}/{folder}/*.meta'):
        r = re.search(r'guid: ([0-9a-f]+)', open(m).read(2000))
        if r: g[r.group(1)] = m[:-5]
    pickle.dump(g, open(cache, 'wb'))
    return g

@functools.lru_cache(None)
def hs_texmatch():
    p = f'{ROOT}/hs_texmatch.pkl'
    return pickle.load(open(p, 'rb')) if os.path.exists(p) else {}

def texture_path(guid):
    t = metamap('Texture2D').get(guid)
    if t: return t
    return hs_texmatch().get(guid)

def f(v):
    try: return float(v)
    except: return 0.0
