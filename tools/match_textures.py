# Resolve texture GUIDs with no .meta (hksprites export) -> PNG path. Usage: match_textures.py scene.unity ...
import pickle, os, re, sys, collections, struct, glob
import numpy as np
ROOT=os.environ.get('HKDATA', 'hkdata')
spr,tex=pickle.load(open(f'{ROOT}/metamaps.pkl','rb'))
by=pickle.load(open(f'{ROOT}/tex2sprites.pkl','rb'))
AC=pickle.load(open(f'{ROOT}/hs_alpha4.pkl','rb'))
hs=sorted(glob.glob(f'{ROOT}/hs/Texture2D/*.png'))
byname=collections.defaultdict(list)
for p in hs: byname[os.path.splitext(os.path.basename(p))[0]].append(p)
def pngsize(p):
    with open(p,'rb') as f: h=f.read(24)
    return struct.unpack('>II',h[16:24])
prev=pickle.load(open(f'{ROOT}/hs_texmatch.pkl','rb')) if os.path.exists(f'{ROOT}/hs_texmatch.pkl') else {}
missing={k:v for k,v in by.items() if k not in tex and k not in prev}
need=set()
for sc in ([] if sys.argv[1:] == ['--all'] else sys.argv[1:]):
    refs=set(re.findall(r'(?:m_Sprite|sprite): \{fileID: \d+, guid: ([0-9a-f]+)',open(sc).read()))
    for tg,L in missing.items():
        if any(x[2] in refs for x in L): need.add(tg)
if sys.argv[1:]==['--all']: need=set(missing)
res={}
for tg in need:
    L=missing[tg]; rects=[r for _,r,_ in L if r]; names=[n for n,_,_ in L]
    if not rects: continue
    if len(L)==1 and names[0] in byname: res[tg]=byname[names[0]][0]; continue
    mx=max(r[0]+r[2] for r in rects); my=max(r[1]+r[3] for r in rects)
    best=None
    for p,(w,h,a) in AC.items():
        if w<mx-1 or h<my-1: continue
        if len(L)>1 and not (w/2<mx+2 and h/2<my+2): continue
        if len(L)==1 and (w>mx*1.3+8 or h>my*1.3+8): continue
        ins=[];
        cover=np.zeros_like(a)
        for (x,y,rw,rh) in rects:
            x0=int(x/4); y0=int((h-(y+rh))/4); x1=int((x+rw)/4)+1; y1=int((h-y)/4)+1
            sub=a[max(0,y0):y1, max(0,x0):x1]
            if sub.size: ins.append(sub.mean())
            cover[max(0,y0):y1, max(0,x0):x1]=True
        if not ins: continue
        inside=np.mean(ins); outside=a[~cover].mean() if (~cover).any() else 0
        # sprite rects in an atlas are padded: a 1-cell ring just outside each rect should be mostly empty
        ring=[]
        for (x,y,rw,rh) in rects[:300]:
            x0=int(x/4)-1; y0=int((h-(y+rh))/4)-1; x1=int((x+rw)/4)+1; y1=int((h-y)/4)+1
            for sl in (a[max(0,y0),max(0,x0):x1] if y0>=0 else None, a[min(a.shape[0]-1,y1),max(0,x0):x1] if y1<a.shape[0] else None,
                       a[max(0,y0):y1,max(0,x0)] if x0>=0 else None, a[max(0,y0):y1,min(a.shape[1]-1,x1)] if x1<a.shape[1] else None):
                if sl is not None and sl.size: ring.append(sl.mean())
        ringv=np.mean(ring) if ring else 0
        sc=inside-outside-0.5*ringv
        if best is None or sc>best[0]: best=(sc,p)
    if best and best[0]>0.3: res[tg]=best[1]
    print(tg, len(L), names[:3], '->', os.path.basename(best[1]) if best else None, round(best[0],3) if best else None, flush=True)
prev.update(res); pickle.dump(prev,open(f'{ROOT}/hs_texmatch.pkl','wb'))
print('resolved',len(res),'of',len(need))
