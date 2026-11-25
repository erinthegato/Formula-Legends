"""Formula Legends -- the trackside objects made in Blender (tools/blender/trackside.py) as game models.

    python3 tools/gen_trackside.py [preview_dir]

Reads tools/blender/props/<name>.json and writes HD/models/<name>.emm (two LODs). Every object is
coloured from swatches only, so each gets one 64x64 texture (8x8 cells of flat colour): about 5 KB
instead of the 300 KB of a painted 512x512 atlas.
"""
import json, os, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from gen_props import Atlas, Kit, write_emm, OUT      # noqa: E402

PROPS = ('gantry', 'bleacher', 'tent', 'flagpanel', 'crane', 'tecpro')
# the circuit buildings share one 512 px atlas, stored once in grandstand.emm (Renderer::loadCommon
# hands it to the others): flat swatches plus the pictures painted below
SHARED = ('grandstand', 'billboard', 'ad_bridge', 'race_control', 'hospitality')
ATLAS = 512
SRC = os.path.join(HERE, 'blender', 'props')


SHIRTS = [(200, 40, 35), (35, 80, 180), (240, 210, 50), (40, 150, 60), (235, 235, 230), (30, 30, 35), (230, 120, 30), (150, 40, 140)]
SKIN = [(225, 180, 150), (190, 140, 105), (130, 90, 65), (235, 200, 170)]


def crowd(a):
    """a 64 x 8 pixel strip of fans: a head over a shirt every two pixels, seats showing between"""
    x0, y0, x1, y1 = [int(v) for v in a.add('crowd', 8, 1)]
    rng = np.random.RandomState(7)
    px = a.img.load()
    for x in range(x0 - 1, x1 + 1):
        for y in range(y0 - 1, y1 + 1):
            px[x, y] = (70, 70, 74)                                        # seats
        if (x - x0) % 2 == 0 and rng.rand() < 0.85:
            shirt, skin = SHIRTS[rng.randint(len(SHIRTS))], SKIN[rng.randint(len(SKIN))]
            top = y0 - 1 + rng.randint(0, 2)
            for y in range(top, y1 + 1):
                px[x, y] = skin if y < top + 2 else shirt


def build(name, prev=None):
    d = json.load(open(os.path.join(SRC, name + '.json')))
    a = Atlas(64, 8)
    used = {m.split('_')[0] for m in d['materials']}
    if 'crowd' in used:
        crowd(a)
    for sw, col in d['palette'].items():
        if sw in used and sw != 'crowd':
            a.solid(sw, col)
    kits = []
    for lod in d['lods']:
        k = Kit()
        P = np.array(lod['p'], float).reshape(-1, 3, 3)
        N = np.array(lod['n'], float).reshape(-1, 3, 3)
        for i in range(len(P)):
            sw, _, shade = d['materials'][lod['m'][i]].partition('_')
            u0, v0, u1, v1 = a.rects[sw]
            if sw == 'crowd':      # people along the row: u follows the strip's length, v its height
                y0, y1 = -4.7, 4.7                                 # the crowd strip spans y = +-(W - 0.3)
                row = int(round(-(P[i, :, 0].mean() + 0.45) / 0.9))
                uv = []
                for x, y, z in P[i]:
                    f = (y - y0) / (y1 - y0)
                    if row % 2: f = 1 - f                         # rows don't line up
                    zb = 0.45 + row * 0.55
                    g = 1 - min(max((z - zb) / 0.55, 0), 1)       # top of the strip = heads
                    uv.append((u0 + f * (u1 - u0), v0 + g * (v1 - v0)))
            else:
                c = ((u0 + u1) / 2, (v0 + v1) / 2)                # flat colour: the middle of its cell
                uv = [c, c, c]
            k._add(P[i], uv, int(shade) / 100.0 if shade else 1.0, N[i])
        kits.append(k)
    n, size = write_emm(os.path.join(OUT, name + '.emm'), [(kits[0], a.img, 64), (kits[1], a.img, 64)])
    kb = os.path.getsize(os.path.join(OUT, name + '.emm')) / 1024
    print(f'{name:10s} triangles {n}  size {size} m  file {kb:.1f} KB')
    if prev:
        a.img.resize((256, 256)).save(os.path.join(prev, f'atlas_{name}.png'))


# ---- the shared trackside atlas
from PIL import Image, ImageDraw, ImageFont          # noqa: E402
FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-Bold.ttf'
MONO = '/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf'
# made-up sponsors: (text, background, lettering)
ADS = [('AURORA RACING', (24, 40, 130), (255, 255, 255)), ('ORBIT', (20, 130, 60), (255, 255, 255)),
       ('VELOCITY', (245, 200, 30), (20, 30, 80)), ('NEXUS TYRES', (210, 25, 25), (255, 255, 255)),
       ('VOLTA ENERGY', (130, 200, 30), (10, 10, 10)), ('KESTREL WATCHES', (15, 15, 15), (220, 180, 70)),
       ('HELIX OIL', (110, 35, 150), (255, 255, 255)), ('FORMULA LEGENDS', (40, 40, 48), (255, 255, 255))]
# where each picture sits (pixels: x0, y0, x1, y1)
PICS = {'ad%d' % (k + 1): ((k % 2) * 256, (k // 2) * 64, (k % 2) * 256 + 256, (k // 2) * 64 + 64) for k in range(8)}
PICS.update(timing=(0, 256, 128, 448), crowd=(128, 256, 384, 288), crowdrows=(384, 256, 512, 352),
            glass=(128, 288, 256, 416))
SW_Y, SW_CELL = 448, 16          # flat colour cells: 32 across, 4 rows at the bottom
SHIRTS = [(200, 40, 35), (35, 80, 180), (240, 210, 50), (40, 150, 60), (235, 235, 230), (30, 30, 35),
          (230, 120, 30), (150, 40, 140), (90, 200, 220), (250, 140, 170)]
SKINS = [(225, 180, 150), (190, 140, 105), (130, 90, 65), (235, 200, 170)]


def fit_text(d, box, text, font_path, colour, max_h):
    x0, y0, x1, y1 = box
    size = max_h
    while size > 8:
        f = ImageFont.truetype(font_path, size)
        l, t, r, b = d.textbbox((0, 0), text, font=f)
        if r - l <= (x1 - x0) * 0.9:
            break
        size -= 1
    d.text(((x0 + x1 - (r - l)) / 2 - l, (y0 + y1 - (b - t)) / 2 - t), text, font=f, fill=colour)


def paint_atlas(swatches):
    img = Image.new('RGB', (ATLAS, ATLAS), (128, 128, 128))
    d = ImageDraw.Draw(img)
    rng = np.random.RandomState(11)
    for k, (text, bg, fg) in enumerate(ADS):            # sponsor boards
        x0, y0, x1, y1 = PICS['ad%d' % (k + 1)]
        d.rectangle((x0, y0, x1 - 1, y1 - 1), fill=bg)
        d.rectangle((x0, y1 - 7, x1 - 1, y1 - 1), fill=tuple(int(c * 0.6) for c in bg))
        if k == 7:
            d.rectangle((x0, y0, x1 - 1, y0 + 5), fill=(220, 30, 30))
        fit_text(d, (x0 + 6, y0 + 4, x1 - 6, y1 - 8), text, FONT, fg, 40)
    x0, y0, x1, y1 = PICS['timing']                     # the timing screen
    d.rectangle((x0, y0, x1 - 1, y1 - 1), fill=(6, 6, 8))
    mono = ImageFont.truetype(MONO, 11)
    d.text((x0 + 6, y0 + 5), 'LAP 23/57', font=mono, fill=(255, 190, 40))
    for i in range(12):
        num = [7, 14, 3, 22, 9, 31, 5, 18, 11, 27, 2, 44][i]
        t = 81.402 + i * 0.37 + rng.rand() * 0.2
        line = '%2d %2d %d:%06.3f' % (i + 1, num, int(t // 60), t % 60)
        d.text((x0 + 4, y0 + 22 + i * 14), line, font=mono, fill=(255, 255, 255) if i else (255, 220, 60))
    x0, y0, x1, y1 = PICS['crowd']                      # a row of seated fans
    d.rectangle((x0, y0, x1 - 1, y1 - 1), fill=(62, 64, 70))
    for px in range(x0 + 2, x1 - 14, 16):
        if rng.rand() < 0.08:
            continue                                    # an empty seat
        sx = px + rng.randint(0, 3)
        shirt = SHIRTS[rng.randint(len(SHIRTS))]; skin = SKINS[rng.randint(len(SKINS))]
        lift = rng.randint(0, 3)
        d.rectangle((sx, y0 + 13 - lift, sx + 12, y1 - 1), fill=shirt)          # shoulders and body
        d.ellipse((sx + 3, y0 + 3 - lift, sx + 9, y0 + 12 - lift), fill=skin)  # head
        if rng.rand() < 0.15:                                                # a raised arm
            d.rectangle((sx + 11, y0 + 1, sx + 13, y0 + 14), fill=shirt)
    x0, y0, x1, y1 = PICS['crowdrows']                  # the whole stand from far away: 8 rows
    d.rectangle((x0, y0, x1 - 1, y1 - 1), fill=(70, 72, 78))
    for r in range(8):
        yy = y0 + r * 12
        d.rectangle((x0, yy + 10, x1 - 1, yy + 11), fill=(150, 150, 148))     # the step
        for px in range(x0, x1 - 3, 4):
            if rng.rand() < 0.1:
                continue
            d.rectangle((px, yy + 5, px + 2, yy + 9), fill=SHIRTS[rng.randint(len(SHIRTS))])
            d.rectangle((px + 1, yy + 2, px + 2, yy + 4), fill=SKINS[rng.randint(len(SKINS))])
    x0, y0, x1, y1 = PICS['glass']                      # tinted glass with the sky in it
    g = Image.new('RGB', (x1 - x0, y1 - y0)); gd = ImageDraw.Draw(g)
    gw, gh = g.size
    for yy in range(gh):
        t = yy / gh
        top = np.array((120, 160, 200)); bot = np.array((25, 38, 58))
        c = top * (1 - t) ** 1.5 + bot * (1 - (1 - t) ** 1.5)
        gd.line((0, yy, gw - 1, yy), fill=tuple(int(v) for v in c))
    for k in range(3):                                  # light streaks
        sx = 20 + k * 38
        for j in range(6):
            gd.line((sx + j, gh - 1, sx + 40 + j, 0), fill=(150, 185, 215) if k != 1 else (110, 145, 180))
    gd.rectangle((0, 0, gw - 1, gh - 1), outline=(18, 20, 24), width=2)   # the frame
    img.paste(g, (x0, y0))
    cells = {}
    for k, (name, col) in enumerate(swatches.items()):  # flat colours
        cx, cy = (k % 32) * SW_CELL, SW_Y + (k // 32) * SW_CELL
        d.rectangle((cx, cy, cx + SW_CELL - 1, cy + SW_CELL - 1), fill=tuple(int(round(c * 255)) for c in col))
        cells[name] = ((cx + SW_CELL / 2) / ATLAS, (cy + SW_CELL / 2) / ATLAS)
    return img, cells


def pic_uv(pic, u, v):
    x0, y0, x1, y1 = PICS[pic]
    x0 += 1; y0 += 1; x1 -= 1; y1 -= 1                 # keep inside the picture
    return ((x0 + u * (x1 - x0)) / ATLAS, (y1 - v * (y1 - y0)) / ATLAS)     # v = 0 at the picture's foot


def build_shared(prev=None):
    data = {n: json.load(open(os.path.join(SRC, n + '.json'))) for n in SHARED}
    used = []
    for d in data.values():
        for m in d['materials']:
            sw = m.split('_')[0]
            if sw != 'decal' and sw not in used:
                used.append(sw)
    pal = next(iter(data.values()))['palette']
    img, cells = paint_atlas({sw: pal[sw] for sw in used})
    total = 0
    for name, d in data.items():
        kits = []
        for lod in d['lods']:
            k = Kit()
            P = np.array(lod['p'], float).reshape(-1, 3, 3)
            N = np.array(lod['n'], float).reshape(-1, 3, 3)
            U = np.array(lod['uv'], float).reshape(-1, 3, 2)
            for i in range(len(P)):
                key = d['materials'][lod['m'][i]]
                if key.startswith('decal_'):
                    uv = [pic_uv(key[6:], u, v) for u, v in U[i]]
                    shade = 1.0
                else:
                    sw, _, sh = key.partition('_')
                    uv = [cells[sw]] * 3
                    shade = int(sh) / 100.0 if sh else 1.0
                k._add(P[i], uv, shade, N[i])
            kits.append(k)
        owner = name == SHARED[0]
        lods = [(kits[0], img if owner else None, ATLAS), (kits[1], None, ATLAS)]
        n, size = write_emm(os.path.join(OUT, name + '.emm'), lods)
        kb = os.path.getsize(os.path.join(OUT, name + '.emm')) / 1024
        total += kb
        print(f'{name:13s} triangles {n}  size {size} m  file {kb:.1f} KB' + ('  (carries the atlas)' if owner else ''))
    print(f'circuit buildings together: {total:.0f} KB')
    if prev:
        img.save(os.path.join(prev, 'trackside_atlas.png'))


if __name__ == '__main__':
    prev = sys.argv[1] if len(sys.argv) > 1 else None
    for name in PROPS:
        build(name, prev)
    if all(os.path.exists(os.path.join(SRC, n + '.json')) for n in SHARED):
        build_shared(prev)
