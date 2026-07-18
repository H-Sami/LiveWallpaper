from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter

size = 256
img = Image.new("RGBA", (size, size), (10, 12, 18, 255))
pixels = img.load()
for y in range(size):
    for x in range(size):
        dx = x / (size - 1)
        dy = y / (size - 1)
        r = int(16 + 40 * dx)
        g = int(18 + 30 * (1 - dy))
        b = int(30 + 70 * dx + 24 * (1 - dy))
        pixels[x, y] = (r, g, b, 255)

glow = Image.new("RGBA", img.size, (0, 0, 0, 0))
g = ImageDraw.Draw(glow)
g.ellipse((110, -30, 300, 160), fill=(100, 92, 255, 105))
g.ellipse((-60, 115, 155, 330), fill=(0, 213, 255, 85))
glow = glow.filter(ImageFilter.GaussianBlur(35))
img = Image.alpha_composite(img, glow)

d = ImageDraw.Draw(img)
d.rounded_rectangle((18, 18, 238, 238), radius=54, outline=(255, 255, 255, 38), width=3)
# Three clean wallpaper waves.
for offset, color, width in [
    (0, (243, 246, 255, 255), 17),
    (28, (118, 104, 255, 255), 13),
    (52, (39, 216, 255, 245), 10),
]:
    points = [(48, 92 + offset), (82, 132 + offset), (116, 91 + offset),
              (151, 131 + offset), (207, 72 + offset)]
    d.line(points, fill=color, width=width, joint="curve")

out = Path(__file__).resolve().parents[1] / "controller" / "Assets"
out.mkdir(parents=True, exist_ok=True)
img.save(out / "LiveWallpaper.png")
img.save(out / "LiveWallpaper.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print(out / "LiveWallpaper.ico")
