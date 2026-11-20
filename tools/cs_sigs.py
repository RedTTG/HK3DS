# Build a map of C# class -> serialized field names from the decompiled Assembly-CSharp, used to identify
# MonoBehaviours in the scene files (AssetRipper keeps only the script guid, and the scripts have no .meta files).
import re, glob, os, json, sys
SRC = os.environ.get('HKDATA', 'hkdata') + '/Assembly-CSharp'
def classes():
    out = {}
    for f in glob.glob(f'{SRC}/**/*.cs', recursive=True):
        txt = open(f, encoding='utf-8', errors='replace').read()
        for m in re.finditer(r'^(?:public |internal )?(?:sealed |abstract )?(?:partial )?class (\w+)(?:\s*:\s*([\w\.<>, ]+))?', txt, re.M):
            name, base = m.group(1), (m.group(2) or '').split(',')[0].strip()
            # fields at one indent level below the class
            body_start = m.end()
            fields = []
            for fm in re.finditer(r'^\t(?:\[[^\]]*\]\s*)*(?:(?:public|private|protected|internal)\s+)?((?:static|const|readonly)\s+)?([\w\.\[\]<>,]+)\s+(\w+)\s*(?:=[^;]*)?;', txt[body_start:], re.M):
                if fm.group(1) and fm.group(1).strip() in ('static', 'const'): continue
                fields.append(fm.group(3))
            out[name] = {'base': base, 'fields': fields}
    return out
if __name__ == '__main__':
    c = classes()
    json.dump(c, open(os.path.join(os.path.dirname(__file__), 'data', 'cs_sigs.json'), 'w'))
    print(len(c), 'classes'); print(c.get('HealthManager', {}).get('fields', [])[:20]); print(c.get('Walker'))

def identify(keys, sigs, cache={}):
    """Best class for a MonoBehaviour's serialized keys (all keys must be fields of the class)."""
    ks = frozenset(k for k in keys if not k.startswith('m_'))
    if ks in cache: return cache[ks]
    best, bs = None, -1
    if ks:
        for name, d in sigs.items():
            fs = set(d['fields'])
            if ks <= fs:
                sc = len(ks) / (len(fs) + 1)
                if sc > bs: best, bs = name, sc
    cache[ks] = best
    return best
