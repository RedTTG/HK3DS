import glob, struct, numpy as np, os, pickle
from PIL import Image
Image.MAX_IMAGE_PIXELS=None
ROOT=os.environ.get('HKDATA', 'hkdata')
out={}
fs=sorted(glob.glob(f'{ROOT}/hs/Texture2D/*.png'))
for i,p in enumerate(fs):
    with open(p,'rb') as f: h=f.read(24)
    w,hh=struct.unpack('>II',h[16:24])
    if w*hh < 256*256: continue
    try:
        im=Image.open(p); im.draft(None,None)
        a=np.asarray(im.getchannel('A') if 'A' in im.getbands() else im.convert('L'))
        out[p]=(w,hh,(a[::4,::4]>8))
    except Exception as e: print('err',p,e)
    if i%100==0: print(i,flush=True)
pickle.dump(out,open(f'{ROOT}/hs_alpha4.pkl','wb'))
print('done',len(out))
