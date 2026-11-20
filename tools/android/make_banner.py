# The Android TV launcher's banner, tools/android/banner.png (320x180 at xhdpi: 640x360): the app
# icon on the menus' night-violet with the title in Cinzel Decorative. usage: python make_banner.py
import os
from PIL import Image, ImageDraw, ImageFont

W, H = 640, 360
img = Image.new("RGBA", (W, H))
d = ImageDraw.Draw(img)
top, bottom = (34, 20, 52), (12, 7, 20)
for y in range(H):
    t = y / (H - 1)
    d.line([(0, y), (W, y)], fill=tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)) + (255,))
TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
icon = Image.open(os.path.join(TOOLS, "linux", "rondorecomp.png")).convert("RGBA").resize((220, 220), Image.LANCZOS)
img.alpha_composite(icon, (24, (H - 220) // 2))
font = ImageFont.truetype(os.path.join(TOOLS, "fonts", "CinzelDecorative-Bold.ttf"), 54)
gold = (226, 190, 120, 255)
for i, word in enumerate(("Rondo", "Recomp")):
    d.text((262, 104 + i * 76), word, font=font, fill=gold)
out = os.path.join(TOOLS, "android", "banner.png")
img.convert("RGB").save(out)
print("wrote", out)
