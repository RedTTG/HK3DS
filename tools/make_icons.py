#!/usr/bin/env python3
"""Plain stand-in icons and banners for the build, made only where you haven't put your own:
  icon.png, icon_hd.png (48x48: the HOME Menu icon of each version), cia/banner.png, cia/banner_hd.png (256x128) and
  cia/banner.wav (the banner's sound: a short silence).
   python make_icons.py <repository folder>
They're plain lettering on black; replace them with your own art any time (the build never overwrites them)."""
import os, sys, struct, wave
from PIL import Image, ImageDraw, ImageFont


def font(size):
    for f in ('DejaVuSans-Bold.ttf', '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 'arial.ttf', 'Arial Bold.ttf'):
        try: return ImageFont.truetype(f, size)
        except OSError: pass
    return ImageFont.load_default()


def card(w, h, lines, sizes):
    im = Image.new('RGBA', (w, h), (12, 14, 22, 255)); d = ImageDraw.Draw(im)
    fs = [font(s) for s in sizes]
    hs = [d.textbbox((0, 0), t, font=f)[3] for t, f in zip(lines, fs)]
    y = (h - sum(hs) - 4 * (len(lines) - 1)) // 2
    for t, f, lh in zip(lines, fs, hs):
        tw = d.textbbox((0, 0), t, font=f)[2]
        d.text(((w - tw) // 2, y), t, font=f, fill=(235, 238, 245, 255)); y += lh + 4
    return im


def main(top):
    made = []
    def want(rel, make):
        p = os.path.join(top, rel)
        if os.path.exists(p): return
        os.makedirs(os.path.dirname(p) or '.', exist_ok=True); make(p); made.append(rel)
    want('icon.png', lambda p: card(48, 48, ['HK', '3DS'], [17, 12]).save(p))
    want('icon_hd.png', lambda p: card(48, 48, ['HK', 'HD'], [17, 12]).save(p))
    want('cia/banner.png', lambda p: card(256, 128, ['HK3DS'], [40]).save(p))
    want('cia/banner_hd.png', lambda p: card(256, 128, ['HK3DS', 'HD'], [40, 22]).save(p))
    def silence(p):
        with wave.open(p, 'wb') as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(22050); w.writeframes(b'\0\0' * 11025)
    want('cia/banner.wav', silence)
    print('made ' + ', '.join(made) if made else 'icons and banners: using yours')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
