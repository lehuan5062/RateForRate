"""Makes the itch.io images of Shift Happens from the game's own frames (Saved/Itch/frames, shot by Tools/itch/shoot.ps1) and from typography drawn here.

Nothing in these pictures comes from a generative image model: the board frames are the game's rendering, the wordmark is Roboto (the HUD font, Apache 2.0)
drawn with Pillow, the keycap and the arrow are polygons. Output goes to Saved/Itch/upload (git-ignored); each file is named after its itch.io slot.

  python Tools/itch/make_images.py logo        the transparent logo and the favicon, with a review sheet
  python Tools/itch/make_images.py promo       cover, social, wide cover, banner, thumbnail (GIF + PNG), from the hero frames
  python Tools/itch/make_images.py bg          the page background: a seamless 6 s loop of the follower network (GIF) and its PNG fallback; needs no frames
  python Tools/itch/make_images.py scenes      the screenshot GIFs from the scene frames (flip, plug, night, mod eye) and the results still
  python Tools/itch/make_images.py check       sizes, aspect ratios, loops, first frames and the flash rule of everything in upload/
  python Tools/itch/make_images.py all         logo, promo, bg, scenes, check
  --max-mb N                                   file size cap for every GIF (default 3)
"""
import functools
import glob
import hashlib
import json
import math
import os
import random
import shutil
import subprocess
import sys

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
ITCH = os.path.join(ROOT, 'Saved', 'Itch')
OUT = os.path.join(ITCH, 'upload')
REVIEW = os.path.join(ITCH, 'review')
FRAMES = os.path.join(ITCH, 'frames')
TMP = os.path.join(ITCH, 'tmp')
UE = os.environ.get('SHIFT_UE_ROOT', r'C:\Program Files\Epic Games\UE_5.8')
FONT_DIR = os.path.join(UE, 'Engine', 'Content', 'Slate', 'Fonts')
MAX_BYTES = 3 * 1024 * 1024

# the game's palette (UI/SShiftHud.cpp, FPal)
INK = (0x1B, 0x19, 0x30)
TEAL = (0x1F, 0xB6, 0xA6)
PRESSED = (0x7B, 0x55, 0xFF)
VIOLET = (0x8B, 0x6C, 0xFF)
AMBER = (0xF2, 0xB1, 0x34)
RED = (0xFF, 0x4D, 0x5A)
CAP = (0xFB, 0xFA, 0xF6)
CAP_BASE = (0xB9, 0xB3, 0xA6)
PAPER = (0xEE, 0xEB, 0xE4)
NIGHT_BG = (0x0A, 0x0D, 0x1C)
WHITE = (255, 255, 255)
SS = 3   # supersampling for everything drawn here

TAGLINE = 'A game about winning a game jam by shifting the votes.'


def font(name, size):
    return ImageFont.truetype(os.path.join(FONT_DIR, name), int(round(size)))


def ensure(*dirs):
    for d in dirs:
        os.makedirs(d, exist_ok=True)


def luminance(rgb):
    def lin(c):
        c = c / 255.0
        return c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = rgb[:3]
    return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b)


def contrast(a, b):
    la, lb = luminance(a), luminance(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)


# --------------------------------------------------------------------------------------------------------------- the mark, logo, favicon

def arrow_points(cx, cy, size):
    pts = [(0.50, 0.04), (0.94, 0.52), (0.69, 0.52), (0.69, 0.96), (0.31, 0.96), (0.31, 0.52), (0.06, 0.52)]
    return [(cx + (x - 0.5) * size, cy + (y - 0.5) * size) for x, y in pts]


def keycap(size, pressed=False, ink=INK, heavy=1.0):
    """The title screen's `shift` keycap without its label: a white key on a darker base, the outline arrow; pressed = the key sunk and the arrow violet."""
    s = size * SS
    img = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    r = s * 0.22
    lift = s * (0.0 if pressed else 0.07)
    d.rounded_rectangle((s * 0.04, s * 0.19, s * 0.96, s * 0.96), r, fill=CAP_BASE, outline=ink, width=int(s * 0.035 * heavy))
    cap_top = s * 0.05 + (s * 0.07 - lift)
    cap_bot = s * 0.86 + s * 0.07 - lift
    d.rounded_rectangle((s * 0.04, cap_top, s * 0.96, cap_bot), r, fill=CAP, outline=ink, width=int(s * 0.035 * heavy))
    glyph = PRESSED if pressed else ink
    pts = arrow_points(s * 0.5, (cap_top + cap_bot) / 2, s * 0.46)
    d.line(pts + [pts[0]], fill=glyph, width=int(s * 0.045 * heavy), joint='curve')
    return img.resize((size, size), Image.LANCZOS)


def logo(width=1600, pad=44, pressed=False, tracking=0.018):
    """Transparent horizontal wordmark: the keycap, `SHIFT` in teal and `HAPPENS` in white, every letter outlined in the game's ink, so it reads on white
    (the outline) and on black (the fill)."""
    gap_word = 0.30
    stroke_frac = 0.075
    size = 400
    while True:
        f = font('Roboto-Black.ttf', size * SS)
        wa = sum(f.getlength(c) for c in 'SHIFT') / SS + tracking * size * 5
        wb = sum(f.getlength(c) for c in 'HAPPENS') / SS + tracking * size * 7
        total = size * 1.05 + 0.28 * size + wa + gap_word * size + wb + 2 * stroke_frac * size
        if total <= width - 2 * pad:
            break
        size -= 4
    stroke = max(2, int(stroke_frac * size * SS))
    cap_h = -f.getbbox('H', anchor='ls')[1] / SS
    mark_px = int(size * 1.05)
    height = int(mark_px + pad * 1.2)
    W, H = width * SS, height * SS
    img = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    x = int(((width - total) / 2 + stroke_frac * size) * SS)
    cy = H / 2
    km = keycap(mark_px, pressed).resize((mark_px * SS, mark_px * SS), Image.LANCZOS)
    img.alpha_composite(km, (x, int(cy - mark_px * SS / 2)))
    x += int((mark_px + 0.28 * size) * SS)
    base = cy + cap_h * SS / 2

    def word(text, x, fill):
        for c in text:
            d.text((x, base), c, font=f, fill=fill + (255,), stroke_width=stroke, stroke_fill=INK + (255,), anchor='ls')
            x += f.getlength(c) + tracking * size * SS
        return x

    x = word('SHIFT', x, TEAL) + gap_word * size * SS
    word('HAPPENS', x, WHITE)
    return img.resize((width, height), Image.LANCZOS)


def logo_stacked(width=1600, pad=44, pressed=False, tracking=0.018):
    """The wordmark on two lines for the near-square page cover: the keycap and `SHIFT` on line 1, `HAPPENS` on line 2, both centred, same letters and outline as
    the horizontal logo."""
    gap_mark = 0.28
    stroke_frac = 0.075
    size = 400
    while True:
        f = font('Roboto-Black.ttf', size * SS)
        wa = sum(f.getlength(c) for c in 'SHIFT') / SS + tracking * size * 5
        wb = sum(f.getlength(c) for c in 'HAPPENS') / SS + tracking * size * 7
        w1 = size * 1.05 + gap_mark * size + wa
        if max(w1, wb) + 2 * stroke_frac * size <= width - 2 * pad:
            break
        size -= 4
    stroke = max(2, int(stroke_frac * size * SS))
    cap_h = -f.getbbox('H', anchor='ls')[1] / SS
    mark_px = int(size * 1.05)
    line_gap = 0.30 * size
    height = int(mark_px + line_gap + cap_h + 2 * pad)
    W, H = width * SS, height * SS
    img = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    def word(text, x, base, fill):
        for c in text:
            d.text((x, base), c, font=f, fill=fill + (255,), stroke_width=stroke, stroke_fill=INK + (255,), anchor='ls')
            x += f.getlength(c) + tracking * size * SS

    top = pad
    x = (width - w1) / 2 * SS
    img.alpha_composite(keycap(mark_px, pressed).resize((mark_px * SS, mark_px * SS), Image.LANCZOS), (int(x), int(top * SS)))
    word('SHIFT', x + (mark_px + gap_mark * size) * SS, (top + mark_px / 2 + cap_h / 2) * SS, TEAL)
    word('HAPPENS', (width - wb) / 2 * SS, (top + mark_px + line_gap + cap_h) * SS, WHITE)
    return img.resize((width, height), Image.LANCZOS)


def favicon(size=512):
    """The website favicon: the shift arrow alone, as large as the canvas allows, teal with a thick outline split in paper and night like the app icon's background,
    on a transparent background (teal shows on a light and on a dark tab, and one half of the outline shows on either). The disc and ring of the app icon made the
    arrow too small in a 16 px tab."""
    s = size * 4
    img = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    pts = arrow_points(s / 2, s / 2, s * 0.90)
    d.polygon(pts, fill=TEAL + (255,))
    ring = Image.new('L', (s, s), 0)
    ImageDraw.Draw(ring).line(pts + [pts[0]], fill=255, width=int(s * 0.075), joint='curve')
    tile = Image.new('RGBA', (s, s), NIGHT_BG + (255,))
    ImageDraw.Draw(tile).polygon([(0, 0), (0.62 * s, 0), (0.38 * s, s), (0, s)], fill=PAPER + (255,))
    img.paste(tile, (0, 0), ring)
    return img.resize((size, size), Image.LANCZOS)


def icon(size=512, split=True):
    """The one icon, for the exe (Build/Windows/Application.ico) and for the website favicon: the voter's disc and conviction ring (three quarters full) with the
    game's shift arrow where the face was, on the day / night split of the cover (`split`) or on a transparent canvas. The arrow is the game's name and
    mark, and as a bold white shape on teal it still reads in a 16 px browser tab where a face did not."""
    s = size * 4
    img = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    if split:
        tile = Image.new('RGBA', (s, s), NIGHT_BG + (255,))
        ImageDraw.Draw(tile).polygon([(0, 0), (0.62 * s, 0), (0.38 * s, s), (0, s)], fill=PAPER + (255,))
        mask = Image.new('L', (s, s), 0)
        ImageDraw.Draw(mask).rounded_rectangle((0, 0, s - 1, s - 1), int(s * 0.22), fill=255)
        img.paste(tile, (0, 0), mask)
    d = ImageDraw.Draw(img)
    c = s / 2
    R = s * (0.347 if split else 0.355)   # 0.347: the ring (outer radius 1.27 R, width 0.17 R) ends one ring-width from the tile edge

    def box(r):
        return (c - r, c - r, c + r, c + r)

    d.ellipse(box(R * 1.27), outline=(128, 128, 150, 120), width=int(R * 0.17))
    d.arc(box(R * 1.27), -90, 180, fill=TEAL + (255,), width=int(R * 0.17))
    d.ellipse(box(R), fill=TEAL + (255,), outline=INK + (255,), width=int(R * 0.09))
    pts = arrow_points(c, c, R * 1.36)   # the shift arrow alone, a solid white block with an ink outline so it is still a shape at 16 px
    d.polygon(pts, fill=WHITE + (255,))
    d.line(pts + [pts[0]], fill=INK + (255,), width=int(R * 0.085), joint='curve')
    return img.resize((size, size), Image.LANCZOS)


def on(bg, im, pad=30):
    canvas = Image.new('RGBA', (im.width + 2 * pad, im.height + 2 * pad), bg + (255,))
    canvas.alpha_composite(im, (pad, pad))
    return canvas


def review_logo(lg, fav):
    bgs = [('white', WHITE), ('black', (0, 0, 0)), ('paper', PAPER), ('night', NIGHT_BG)]
    tiles = [on(c, lg) for _, c in bgs]
    w = tiles[0].width
    sheet = Image.new('RGBA', (w * 2, tiles[0].height * 2 + 180), (128, 128, 128, 255))
    for i, t in enumerate(tiles):
        sheet.paste(t, ((i % 2) * w, (i // 2) * t.height))
    # the favicon on a light and on a dark browser tab, at 128 / 64 / 32 / 16 px, and the 16 px version enlarged with hard pixels (what the tab really shows)
    y = tiles[0].height * 2 + 20
    for bgc, x0 in (((222, 225, 230), 20), ((53, 54, 58), 760)):
        sheet.paste(Image.new('RGB', (700, 150), bgc), (x0 - 10, y - 10))
        x = x0
        for px in (128, 64, 32, 16):
            sheet.alpha_composite(fav.resize((px, px), Image.LANCZOS), (x, y))
            x += px + 24
        sheet.alpha_composite(fav.resize((16, 16), Image.LANCZOS).resize((128, 128), Image.NEAREST), (x + 20, y))
    p = os.path.join(REVIEW, 'logo_favicon.png')
    sheet.convert('RGB').save(p)
    print('review sheet:', p)


def make_logo():
    lg, fav, app = logo(), favicon(), icon()
    lg.save(os.path.join(OUT, '10_logo_%dx%d.png' % lg.size))
    fav.save(os.path.join(OUT, '08_favicon_512.png'))
    # the app icon (the disc, ring and arrow on the split) as a Windows icon with every size the shell asks for; copy it to Build/Windows/Application.ico to make
    # it the game's icon (Unreal Engine picks that file up at the next build)
    app.save(os.path.join(OUT, '08_app_icon.ico'), sizes=[(s, s) for s in (16, 24, 32, 48, 64, 128, 256)])
    if os.path.exists(os.path.join(OUT, '08_favicon.ico')):
        os.remove(os.path.join(OUT, '08_favicon.ico'))   # the name from when one icon served both
    review_logo(lg, fav)
    for name, bg in (('white', WHITE), ('black', (0, 0, 0))):
        print('contrast vs %s: teal fill %.1f, white fill %.1f, ink outline %.1f' % (name, contrast(TEAL, bg), contrast(WHITE, bg), contrast(INK, bg)))
    print('logo', lg.size, 'favicon', fav.size)


# --------------------------------------------------------------------------------------------------------------- GIF encoder

def run(cmd, **kw):
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, **kw)
    if r.returncode != 0:
        raise RuntimeError('%s failed:\n%s' % (cmd[0], r.stdout[-1500:]))
    return r.stdout


def encode_gif(frames, out, fps, widths, max_bytes, colors=(256, 192, 128, 96), fps_steps=(1.0,), bayer=4, quiet=False, dither='bayer'):
    """Encodes the frame files (PNG paths, in order) to a looping GIF. Tries the widest width first and the most colours, then fewer colours, then a lower
    frame rate (every n-th frame), then a narrower width, until the file fits `max_bytes`. Returns (path, width, fps, colours, bytes)."""
    ensure(TMP)
    best = None
    for width in widths:
        for step in fps_steps:
            sel = [frames[int(round(i * step))] for i in range(int(len(frames) / step)) if int(round(i * step)) < len(frames)] if step != 1.0 else list(frames)
            seq = os.path.join(TMP, 'seq_' + os.path.splitext(os.path.basename(out))[0])   # one scratch folder per output, so two runs can go side by side
            shutil.rmtree(seq, ignore_errors=True)
            ensure(seq)
            for i, f in enumerate(sel):
                shutil.copyfile(f, os.path.join(seq, '%04d.png' % i))
            for nc in colors:
                use = 'dither=none' if dither == 'none' else 'dither=bayer:bayer_scale=%d' % bayer
                vf = 'scale=%s:-2:flags=lanczos,split[a][b];[a]palettegen=stats_mode=diff:max_colors=%d[p];[b][p]paletteuse=%s:diff_mode=rectangle' % (
                    width if width else 'iw', nc, use)
                run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-framerate', repr(fps / step), '-i', os.path.join(seq, '%04d.png'), '-vf', vf, '-loop', '0', out])
                size = os.path.getsize(out)
                if not quiet:
                    print('    %s: width %s, %.1f fps, %d colours -> %.2f MB' % (os.path.basename(out), width or 'src', fps / step, nc, size / 1048576))
                if size <= max_bytes:
                    return out, width, fps / step, nc, size
                best = (out, width, fps / step, nc, size)
    print('WARNING: %s does not fit %.1f MB even at the smallest setting' % (os.path.basename(out), max_bytes / 1048576))
    return best


def gif_frames(path, small=None):
    """All frames as RGB images. With `small` (w, h) every frame but the first is shrunk to it (a 2560x1440 loop of 60 frames would otherwise be 660 MB)."""
    im = Image.open(path)
    n = getattr(im, 'n_frames', 1)
    out = []
    for i in range(n):
        im.seek(i)
        fr = im.convert('RGB')
        out.append(fr.resize(small, Image.BOX) if small and i > 0 else fr.copy())
    return out


# --------------------------------------------------------------------------------------------------------------- the day / night hero

def hero():
    a = Image.open(os.path.join(FRAMES, 'hero_day', 'f_2200.png')).convert('RGB')
    b = Image.open(os.path.join(FRAMES, 'hero_night', 'f_2200.png')).convert('RGB')
    return a, b


def crop_layer(img, cx, cy, wsrc, size):
    """A crop `wsrc` pixels wide centred on cx, cy of the 4K hero, scaled to `size`."""
    w, h = size
    hsrc = wsrc * h / w
    box = (int(cx - wsrc / 2), int(cy - hsrc / 2), int(cx + wsrc / 2), int(cy + hsrc / 2))
    return img.crop(box).resize(size, Image.LANCZOS)


def diag_mask(size, p, slant=0.16):
    """Day where the mask is 255: left of a slanted line whose middle is at p of the width (the top leans right, a slash)."""
    w, h = size
    s = 3
    m = Image.new('L', (w * s, h * s), 0)
    xt = (p + slant / 2) * w * s
    xb = (p - slant / 2) * w * s
    ImageDraw.Draw(m).polygon([(0, 0), (xt, 0), (xb, h * s), (0, h * s)], fill=255)
    return m.resize((w, h), Image.LANCZOS)


def split_line(img, size, p, slant=0.16):
    w, h = size
    s = 3
    layer = Image.new('RGBA', (w * s, h * s), (0, 0, 0, 0))
    xt = (p + slant / 2) * w * s
    xb = (p - slant / 2) * w * s
    ImageDraw.Draw(layer).line([(xt, 0), (xb, h * s)], fill=(255, 255, 255, 190), width=max(3, int(w * 0.0035 * s)))
    img.alpha_composite(layer.resize((w, h), Image.LANCZOS))


def shadowed(base, overlay, pos, blur=10, alpha=0.55):
    """Pastes an RGBA overlay with a soft dark shadow so it separates from a busy picture."""
    sh = Image.new('RGBA', base.size, (0, 0, 0, 0))
    a = overlay.split()[3].point(lambda v: int(v * alpha))
    black = Image.new('RGBA', overlay.size, (0, 0, 0, 255))
    black.putalpha(a)
    sh.alpha_composite(black, pos)
    base.alpha_composite(sh.filter(ImageFilter.GaussianBlur(blur)))
    base.alpha_composite(overlay, pos)


def text_outline(draw, xy, text, f, fill=WHITE, stroke=INK, sw=4, anchor='mm'):
    draw.text(xy, text, font=f, fill=fill + (255,), stroke_width=sw, stroke_fill=stroke + (255,), anchor=anchor)


PROMOS = {
    # name: (slot file stem, size, crop centre in the 4K hero, crop width, logo width fraction, tagline)
    'cover': ('01_cover_630x500', (630, 500), (1760, 1090), 1800, 0.72, False),
    'social': ('07_social_1200x630', (1200, 630), (1860, 1090), 2500, 0.66, True),
    'wide': ('09_wide_cover_2100x900', (2100, 900), (1900, 1100), 3500, 0.50, False),
    'banner': ('12_banner_1200x300', (1200, 300), (1900, 1090), 3200, 0.62, False),
    'devlog': ('14_devlog_cover_1280x720', (1280, 720), (1860, 1080), 2900, 0.66, True),   # the devlog post's cover image: 16:9, at least 500 px wide; the trailer thumbnail animated
    'thumb': ('11_trailer_thumbnail_1280x720', (1280, 720), (1860, 1080), 2900, 0.66, True),
}


TAGLINE_IMAGE = 'Winning game jams by shifting votes'


@functools.lru_cache(maxsize=None)
def tagline_text(target_w, max_w):
    """The tagline as one line of white Roboto Bold with an ink outline, like the logo's letters, and no box: the outline makes it readable on the paper half and on
    the night half of the split, and a line this size under the logo stays a caption instead of competing with it. (An opaque plate, then a see-through one,
    both took the focus away from the logo.)"""
    probe = font('Roboto-Bold.ttf', 100 * SS)
    c = probe.getlength(TAGLINE_IMAGE) / SS / 100.0   # the line, in em
    sw = 0.11   # outline, in em
    F = min(target_w, max_w) / (c + 2 * sw)
    f = font('Roboto-Bold.ttf', F * SS)
    stroke = max(2, int(sw * F * SS))
    x0, y0, x1, y1 = ImageDraw.Draw(Image.new('L', (8, 8))).textbbox((0, 0), TAGLINE_IMAGE, font=f, anchor='ls', stroke_width=stroke)
    pad = 2 * SS
    img = Image.new('RGBA', (int(x1 - x0) + 2 * pad, int(y1 - y0) + 2 * pad), (0, 0, 0, 0))
    ImageDraw.Draw(img).text((pad - x0, pad - y0), TAGLINE_IMAGE, font=f, fill=WHITE + (255,), stroke_width=stroke, stroke_fill=INK + (255,), anchor='ls')
    return img.resize((img.width // SS, img.height // SS), Image.LANCZOS)


def promo_frame(key, day, night, p, pressed):
    stem, size, (cx, cy), wsrc, lfrac, tag = PROMOS[key]
    W, H = size
    a = crop_layer(day, cx, cy, wsrc, size)
    b = crop_layer(night, cx, cy, wsrc, size)
    img = Image.composite(a, b, diag_mask(size, p)).convert('RGBA')
    split_line(img, size, p)
    lg = (logo_stacked if key == 'cover' else logo)(int(W * lfrac) + 2 * 44, pad=44, pressed=pressed)   # the near-square page cover has the name on two lines
    lg = lg.crop(lg.getbbox())
    blur = max(6, W // 90)
    if not tag:
        shadowed(img, lg, ((W - lg.width) // 2, int(H * 0.5 - lg.height / 2)), blur=blur)
        return img.convert('RGB')
    line = tagline_text(int(lg.width * 0.80), int(W * 0.90))
    gap = int(lg.height * 0.16)
    top = int((H - (lg.height + gap + line.height)) / 2)
    shadowed(img, lg, ((W - lg.width) // 2, top), blur=blur)
    shadowed(img, line, ((W - line.width) // 2, top + lg.height + gap), blur=max(3, W // 300), alpha=0.5)
    return img.convert('RGB')


def sweep(n, amp=0.20, mid=0.5):
    """p(t) over one period: starts at the finished still (mid) and swings gently, so a loop is seamless and the picture never flips as a whole."""
    return [mid + amp * math.sin(2 * math.pi * i / n) for i in range(n)]


def make_promos(max_bytes):
    day, night = hero()
    ensure(OUT, TMP)
    fps = 12.5
    n = 50   # 4 s
    for key, (stem, size, *_rest) in PROMOS.items():
        ps = sweep(n)[:1] if key == 'thumb' else sweep(n)   # the YouTube thumbnail is a still
        frames = []
        d = os.path.join(TMP, 'promo_' + key)
        shutil.rmtree(d, ignore_errors=True)
        ensure(d)
        for i, p in enumerate(ps):
            img = promo_frame(key, day, night, p, pressed=(p < 0.45))
            fp = os.path.join(d, 'f_%03d.png' % i)
            img.save(fp)
            frames.append(fp)
        if key != 'devlog':   # the devlog cover's fallback is the trailer thumbnail (same picture), so it has no PNG of its own
            shutil.copyfile(frames[0], os.path.join(OUT, stem + '.png'))
        if key == 'thumb':
            continue
        widths = {'cover': [630], 'social': [1200, 1000, 800], 'wide': [2100, 1680, 1260], 'banner': [1200, 960], 'devlog': [1280, 960]}[key]
        res = encode_gif(frames, os.path.join(OUT, stem + '.gif'), fps, widths, max_bytes, fps_steps=(1.0, 2.0))
        print('%s: %s %.2f MB, %s px, %.1f fps, %d colours' % (key, os.path.basename(res[0]), res[4] / 1048576, res[1], res[2], res[3]))


# --------------------------------------------------------------------------------------------------------------- the page background

BG_COLOURS = [TEAL, TEAL, TEAL, VIOLET, VIOLET, AMBER, RED, (0x8A, 0x9C, 0xE0), (0x8A, 0x9C, 0xE0)]
BG_FRAMES = 60   # one loop: 6 s at 10 fps
BG_FPS = 10
BG_PULSE = 0.80   # brightness of the travelling votes, 1 = full


def mix(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


class Network:
    """A follower network on a torus: it wraps on both axes, so the tile repeats seamlessly, and everything that moves on it is periodic, so the GIF loops
    seamlessly. Units are the pixels of a 1920 x 1920 canvas (`k` scales them to the size asked for); the theme editor's background is a square tile. Twelve
    hubs, bigger and with more followers, spread out; votes flow from the bigger node of an edge to the smaller one."""
    W, H = 1920.0, 1920.0

    def __init__(self, seed=11, spacing=170.0, hubs=12):
        rnd = random.Random(seed)
        self.pts = []
        for _ in range(30000):
            p = (rnd.uniform(0, self.W), rnd.uniform(0, self.H))
            if all(self.dist(p, q) >= spacing for q in self.pts):
                self.pts.append(p)
        n = len(self.pts)
        order = [0]
        while len(order) < hubs:
            order.append(max((i for i in range(n) if i not in order), key=lambda i: min(self.dist(self.pts[i], self.pts[j]) for j in order)))
        self.hub = set(order)
        self.r = [22.0 if i in self.hub else rnd.choice([8.0, 11.0, 11.0, 14.0, 14.0]) for i in range(n)]
        self.col = [rnd.choice(BG_COLOURS) for _ in range(n)]
        edges = set()
        for i in range(n):
            near = sorted((j for j in range(n) if j != i), key=lambda j: self.dist(self.pts[i], self.pts[j]))
            for j in near[:5 if i in self.hub else 2]:
                if self.dist(self.pts[i], self.pts[j]) <= 340:
                    edges.add((min(i, j), max(i, j)))
        self.edges = sorted(edges)
        self.pulses = []   # (source, target, phase, traversals per loop)
        for i, j in self.edges:
            src, dst = (i, j) if self.r[i] >= self.r[j] else (j, i)
            if src in self.hub or rnd.random() < 0.15:
                self.pulses.append((src, dst, rnd.random(), rnd.choice([1, 1, 2])))

    def vec(self, a, b):
        """The shortest vector from a to b across the wrapped tile."""
        dx, dy = b[0] - a[0], b[1] - a[1]
        if dx > self.W / 2:
            dx -= self.W
        elif dx < -self.W / 2:
            dx += self.W
        if dy > self.H / 2:
            dy -= self.H
        elif dy < -self.H / 2:
            dy += self.H
        return dx, dy

    def dist(self, a, b):
        return math.hypot(*self.vec(a, b))


def bg_static(net, size):
    """Everything that does not move, drawn at 3x and box-filtered down: flat colours, no alpha, so the lines stay solid at any size (the first version drew
    7/255-alpha lines, which quantised into dashes)."""
    W, H = size
    k = W / net.W
    s = 3
    S = k * s   # design unit -> supersampled pixel
    base = NIGHT_BG
    dot_c = mix(base, WHITE, 0.13)
    edge_c = mix(base, (160, 170, 255), 0.17)
    img = Image.new('RGB', (W * s, H * s), base)
    d = ImageDraw.Draw(img)
    # the night board's dot grid; 60 units divide 1920, so it continues across the tile edges (the dots on the edge are halves that join)
    r = 1.7 * S
    for gx in range(0, int(net.W) + 1, 60):
        for gy in range(0, int(net.H) + 1, 60):
            d.ellipse((gx * S - r, gy * S - r, gx * S + r, gy * S + r), fill=dot_c)

    def shifts():
        for ox in (-1, 0, 1):
            for oy in (-1, 0, 1):
                yield ox * W * s, oy * H * s

    for i, j in net.edges:
        a = net.pts[i]
        vx, vy = net.vec(a, net.pts[j])
        for ox, oy in shifts():
            x1, y1, x2, y2 = a[0] * S + ox, a[1] * S + oy, (a[0] + vx) * S + ox, (a[1] + vy) * S + oy
            if max(x1, x2) < 0 or min(x1, x2) > W * s or max(y1, y2) < 0 or min(y1, y2) > H * s:
                continue
            d.line([(x1, y1), (x2, y2)], fill=edge_c, width=max(1, int(round(1.8 * S))))
    for i, (x, y) in enumerate(net.pts):
        rad, c = net.r[i], net.col[i]
        fill, ring = mix(base, c, 0.42), mix(base, c, 0.30)
        for ox, oy in shifts():
            cx, cy = x * S + ox, y * S + oy
            reach = (rad + 8) * S
            if cx + reach < 0 or cx - reach > W * s or cy + reach < 0 or cy - reach > H * s:
                continue
            rr = (rad + 6.5) * S
            d.ellipse((cx - rr, cy - rr, cx + rr, cy + rr), outline=ring, width=max(1, int(round(1.6 * S))))
            rr = rad * S
            d.ellipse((cx - rr, cy - rr, cx + rr, cy + rr), fill=fill)
            if rad >= 14:   # the bigger voters have the game's face: eyes and pupils, a smile
                eye, pupil = mix(base, WHITE, 0.45), mix(fill, base, 0.65)
                for sx in (-1, 1):
                    ex, ey, er = cx + sx * 0.36 * rr, cy - 0.14 * rr, 0.21 * rr
                    d.ellipse((ex - er, ey - er, ex + er, ey + er), fill=eye)
                    px, py, pr = ex + 0.05 * rr, ey + 0.03 * rr, 0.105 * rr
                    d.ellipse((px - pr, py - pr, px + pr, py + pr), fill=pupil)
                d.arc((cx - 0.40 * rr, cy - 0.02 * rr, cx + 0.40 * rr, cy + 0.50 * rr), 25, 155, fill=mix(fill, base, 0.6), width=max(1, int(round(0.07 * rr))))
    return img.reduce(s)


def sprite(img, cx, cy, ext, col, draw):
    """Draws a small RGBA sprite at 4x with `draw(d, x, y, S)` (x, y = its centre on the supersampled patch), box-filters it down and pastes it at every wrapped
    position of the tile, so a sprite on the edge continues on the other side. The patch is pre-filled with the sprite's colour at alpha 0, so the filter does
    not darken its edges."""
    S = 4
    W, H = img.size
    n = int(math.ceil(ext * 2)) + 6
    for wx in (-W, 0, W):
        for wy in (-H, 0, H):
            px, py = cx + wx, cy + wy
            if px + ext < 0 or px - ext > W or py + ext < 0 or py - ext > H:
                continue
            x0, y0 = int(math.floor(px - n / 2.0)), int(math.floor(py - n / 2.0))
            patch = Image.new('RGBA', (n * S, n * S), col + (0,))
            draw(ImageDraw.Draw(patch), (px - x0) * S, (py - y0) * S, S)
            patch = patch.reduce(S)
            img.paste(patch.convert('RGB'), (x0, y0), patch.getchannel('A'))


def bg_frame(net, static, t):
    """The still plus the votes at frame t: a bright dot with a short tail leaves the rim of the bigger node, runs along the edge and dives into the smaller one,
    which flashes and sends out a ring. u = the position on the edge, periodic in the loop, so frame BG_FRAMES equals frame 0."""
    W, H = static.size
    k = W / net.W
    img = static.copy()
    ph = t / float(BG_FRAMES)
    for src, dst, phase, trav in net.pulses:
        a, b = net.pts[src], net.pts[dst]
        vx, vy = net.vec(a, b)
        L = math.hypot(vx, vy)
        u = (trav * ph + phase) % 1.0
        rim_s, rim_d = net.r[src] + 8.5, net.r[dst] + 8.5
        lo, hi = rim_s / L, 1.0 - rim_d / L
        if hi <= lo:
            continue
        col = mix(net.col[src], WHITE, 0.55)
        if lo <= u <= hi:
            fade = min(1.0, (u - lo) / 0.04, (hi - u) / 0.04) * BG_PULSE
            ux, uy = vx / L, vy / L

            def comet(d, cx, cy, S, u=u, L=L, ux=ux, uy=uy, fade=fade, col=col, rim_s=rim_s):
                for dist, al, rad in ((21.0, 0.14, 1.6), (14.0, 0.28, 2.2), (7.0, 0.50, 2.9), (0.0, 1.0, 3.6)):   # the tail first, the head on top
                    if u * L - dist < rim_s:
                        continue
                    x, y, r = cx - ux * dist * k * S, cy - uy * dist * k * S, rad * k * S
                    d.ellipse((x - r, y - r, x + r, y + r), fill=col + (int(255 * al * fade),))

            sprite(img, ((a[0] + vx * u) * k) % W, ((a[1] + vy * u) * k) % H, 26 * k, col, comet)
        v = (u - hi) % 1.0
        if v < 0.16:   # just arrived: the target flashes and a ring runs out of it
            q = v / 0.16
            rc = mix(net.col[src], WHITE, 0.35)
            tx, ty = (b[0] * k) % W, (b[1] * k) % H

            def ripple(d, cx, cy, S, q=q, rc=rc, rad=net.r[dst]):
                r = (rad + 6.5 + 20 * q) * k * S
                d.ellipse((cx - r, cy - r, cx + r, cy + r), outline=rc + (int(255 * 0.6 * BG_PULSE * (1 - q) ** 1.4),), width=max(1, int(round(1.6 * k * S))))

            sprite(img, tx, ty, (net.r[dst] + 30) * k, rc, ripple)
            if q < 0.35:
                def flash(d, cx, cy, S, q=q, rad=net.r[dst]):
                    r = rad * k * S
                    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(255, 255, 255, int(255 * 0.22 * (1 - q / 0.35))))

                sprite(img, tx, ty, (net.r[dst] + 2) * k, WHITE, flash)
    return img


def make_background(max_bytes):
    """13_background_WxW.gif (a square, seamless 6 s loop, tiles on both axes) and its PNG fallback (= frame 1). 2160 px (the uploader's height limit) if it fits the cap, else 1440."""
    ensure(OUT, TMP)
    net = Network()
    for old in glob.glob(os.path.join(OUT, '13_background*')):
        os.remove(old)
    for W in (2160, 1440):   # itch.io's uploader refuses anything over 3840 x 2160, so a square tile is at most 2160
        H = W
        static = bg_static(net, (W, H))
        d = os.path.join(TMP, 'bg_%d' % W)
        shutil.rmtree(d, ignore_errors=True)
        ensure(d)
        frames = []
        for t in range(BG_FRAMES):
            fp = os.path.join(d, 'f_%03d.png' % t)
            bg_frame(net, static, t).save(fp, compress_level=1)
            frames.append(fp)
        stem = '13_background_%dx%d' % (W, H)
        res = encode_gif(frames, os.path.join(OUT, stem + '.gif'), BG_FPS, [W], max_bytes, colors=(256, 192, 128), fps_steps=(1.0, 2.0), dither='none')
        Image.open(frames[0]).convert('RGB').save(os.path.join(OUT, stem + '.png'), optimize=True)
        print('background: %s %.2f MB, %dx%d, %.1f fps, %d colours, %d nodes, %d edges, %d pulses' % (
            os.path.basename(res[0]), res[4] / 1048576, W, H, res[2], res[3], len(net.pts), len(net.edges), len(net.pulses)))
        if res[4] <= max_bytes:
            break


# --------------------------------------------------------------------------------------------------------------- the screenshot scenes

SCENES = {
    # frames dir, output file, fps of the frames, widths ladder
    'flip': ('flip', '02_screenshot_shift_flip.gif', 20, [800, 720, 640]),
    'plug': ('plug', '03_screenshot_plug.gif', 20, [800, 720, 640]),
    'night': ('night', '04_screenshot_night_shift.gif', 20, [800, 720, 640]),
    'modeye': ('modeye', '05_screenshot_mod_eye.gif', 20, [800, 720, 640]),
}


def make_scenes(max_bytes):
    ensure(OUT)
    for key, (d, name, fps, widths) in SCENES.items():
        frames = sorted(glob.glob(os.path.join(FRAMES, d, 'f_*.png')))
        if not frames:
            print('scene %s: no frames in %s (shoot it first)' % (key, d))
            continue
        res = encode_gif(frames, os.path.join(OUT, name), fps, widths, max_bytes, fps_steps=(1.0, 1.25, 1.5))
        print('%s: %s %.2f MB, %s px, %.1f fps, %d colours, %d source frames' % (key, name, res[4] / 1048576, res[1], res[2], res[3], len(frames)))
    # the results card is the one screenshot that does not move: a 1080p still of the settled card
    stills = sorted(glob.glob(os.path.join(FRAMES, 'results', 'f_*.png')))
    if stills:
        Image.open(stills[0]).convert('RGB').save(os.path.join(OUT, '06_screenshot_results.png'))
        print('results: 06_screenshot_results.png from', os.path.basename(stills[0]))
    else:
        print('results: no frames (shoot the results scene first)')


# --------------------------------------------------------------------------------------------------------------- checks

def flash_count(frames, fps):
    """WCAG 'general flash' estimate: the frame-mean relative luminance series; a flash is an opposing pair of changes of at least 10% of the full range inside
    one second. Returns the most flashes found in any one-second window."""
    lum = np.array([luminance(tuple(np.asarray(f.resize((64, 36))).reshape(-1, 3).mean(axis=0))) for f in frames])
    if len(lum) < 3:
        return 0
    thr = 0.10
    events = []   # indices where the luminance swings by thr against the last extreme, with the direction
    last_ext, direction = lum[0], 0
    for i, v in enumerate(lum):
        if direction >= 0 and v - last_ext >= thr:
            events.append((i, 1))
            direction, last_ext = 1, v
        elif direction <= 0 and last_ext - v >= thr:
            events.append((i, -1))
            direction, last_ext = -1, v
        elif direction == 1 and v > last_ext:
            last_ext = v
        elif direction == -1 and v < last_ext:
            last_ext = v
    worst = 0
    win = int(round(fps))
    for i in range(len(lum)):
        inside = [e for e in events if i <= e[0] < i + win]
        worst = max(worst, len(inside) // 2)
    return worst


def check(max_bytes):
    ensure(OUT, REVIEW)
    rows, problems = [], []
    expect = {
        '01_cover_630x500': ((630, 500), 315 / 250.0, 'gif+png'),
        '07_social_1200x630': ((1200, 630), None, 'gif+png'),
        '09_wide_cover_2100x900': (None, 21 / 9.0, 'gif+png'),
        '12_banner_1200x300': ((1200, 300), None, 'gif+png'),
        '14_devlog_cover_1280x720': ((1280, 720), 16 / 9.0, 'gif+png'),
        '13_background_': (None, 1.0, 'gif+png'),   # square tile
    }
    for f in sorted(glob.glob(os.path.join(OUT, '*'))):
        name = os.path.basename(f)
        stem, ext = os.path.splitext(name)
        key = next((k for k in expect if stem.startswith(k)), None)
        size = os.path.getsize(f)
        im = Image.open(f)
        frames_n = getattr(im, 'n_frames', 1)
        row = {'file': name, 'size': im.size, 'mb': size / 1048576, 'frames': frames_n, 'notes': []}
        if size > max_bytes:
            problems.append('%s is %.2f MB, over the %.1f MB cap' % (name, size / 1048576, max_bytes / 1048576))
        if ext == '.gif':
            if im.info.get('loop', None) is None:
                problems.append('%s does not loop' % name)
            frames = gif_frames(f, small=(320, 180))
            dur = sum(im.seek(i) or im.info.get('duration', 100) for i in range(frames_n)) / 1000.0
            fps = frames_n / dur if dur else 0
            row['notes'].append('%.1f s, %.1f fps' % (dur, fps))
            fl = flash_count(frames, max(1.0, fps))
            row['notes'].append('flashes/s %d' % fl)
            if fl > 3:
                problems.append('%s has %d flashes in a second (limit 3)' % (name, fl))
            png = os.path.join(OUT, '11_trailer_thumbnail_1280x720.png' if stem.startswith('14_devlog') else stem + '.png')
            if key and os.path.exists(png):
                same = np.abs(np.asarray(frames[0], dtype=int) - np.asarray(Image.open(png).convert('RGB'), dtype=int)).mean()
                row['notes'].append('frame 1 vs png %.2f' % same)
                if same > 3.0:
                    problems.append('%s: frame 1 differs from the PNG fallback (%.2f)' % (name, same))
            if stem.startswith('13_background') and len(frames) > 2:
                # a seamless loop: the step from the last frame back to the first is no bigger than an ordinary step between two frames
                arr = [np.asarray(fr.resize((320, 180), Image.BOX), dtype=int) for fr in frames]
                steps = [np.abs(arr[i + 1] - arr[i]).mean() for i in range(len(arr) - 1)]
                closing = np.abs(arr[0] - arr[-1]).mean()
                row['notes'].append('loop step %.3f vs %.3f' % (closing, float(np.mean(steps))))
                if closing > 2.5 * max(steps) + 0.05:
                    problems.append('%s does not loop seamlessly (closing step %.3f, ordinary steps up to %.3f)' % (name, closing, max(steps)))
        if stem.startswith('13_background') and ext == '.png':
            # a seamless tile: the pixel row / column on one edge continues into the opposite edge like any two neighbours
            a = np.asarray(im.convert('RGB'), dtype=int)
            seam = max(np.abs(a[:, 0] - a[:, -1]).mean(), np.abs(a[0] - a[-1]).mean())
            typ = max(np.abs(np.diff(a, axis=1)).mean(), np.abs(np.diff(a, axis=0)).mean())
            row['notes'].append('seam step %.2f vs %.2f' % (seam, typ))
            if seam > 3 * typ + 1:
                problems.append('%s has a visible seam (%.2f against %.2f between neighbours)' % (name, seam, typ))
        if key and ext == '.gif':
            sz, ratio, _ = expect[key]
            if sz and im.size != sz:
                problems.append('%s is %s, expected %s' % (name, im.size, sz))
            if ratio and abs(im.size[0] / im.size[1] - ratio) > 0.01:
                problems.append('%s aspect %.4f, expected %.4f' % (name, im.size[0] / im.size[1], ratio))
        if stem.startswith('06_screenshot_results') and im.size != (1920, 1080):
            problems.append('the results still is %s, expected 1920x1080' % (im.size,))
        if stem.startswith('08_favicon') and im.size[0] != im.size[1]:
            problems.append('favicon is not square')
        if stem.startswith('10_logo'):
            a = np.asarray(im.convert('RGBA'))[:, :, 3]
            row['notes'].append('alpha %d..%d, %.1f:1' % (a.min(), a.max(), im.size[0] / im.size[1]))
            if a.min() != 0 or a.max() != 255:
                problems.append('logo is not transparent with solid letters')
        rows.append(row)
    lines = ['| file | size | MB | frames | notes |', '|---|---|---|---|---|']
    for r in rows:
        lines.append('| %s | %dx%d | %.2f | %d | %s |' % (r['file'], r['size'][0], r['size'][1], r['mb'], r['frames'], '; '.join(r['notes'])))
    report = '\n'.join(lines)
    with open(os.path.join(ITCH, 'report.md'), 'w') as fh:
        fh.write('# itch.io images (cap %.1f MB)\n\n%s\n\n%s\n' % (max_bytes / 1048576, report, ('Problems:\n- ' + '\n- '.join(problems)) if problems else 'All checks passed.'))
    print(report)
    print('\n' + ('PROBLEMS:\n  ' + '\n  '.join(problems) if problems else 'all checks passed'))
    return not problems


def main(argv):
    global MAX_BYTES
    args = [a for a in argv[1:]]
    if '--max-mb' in args:
        i = args.index('--max-mb')
        MAX_BYTES = int(float(args[i + 1]) * 1048576)
        del args[i:i + 2]
    cmd = args[0] if args else 'all'
    ensure(OUT, REVIEW, TMP)
    if cmd in ('logo', 'all'):
        make_logo()
    if cmd in ('promo', 'all'):
        make_promos(MAX_BYTES)
    if cmd in ('bg', 'all'):
        make_background(MAX_BYTES)
    if cmd in ('scenes', 'all'):
        make_scenes(MAX_BYTES)
    if cmd in ('check', 'all'):
        ok = check(MAX_BYTES)
        sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main(sys.argv)
