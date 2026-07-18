from pathlib import Path
from PIL import Image, ImageDraw

SOURCE = Path(r"C:\Users\PC\Desktop\app icon.png")
ASSET_DIR = Path(__file__).resolve().parents[1] / "controller" / "Assets"
PNG = ASSET_DIR / "LiveWallpaper.png"
ICO = ASSET_DIR / "LiveWallpaper.ico"

# The supplied PNG contains an opaque checkerboard around the artwork. Crop the
# actual rounded-square logo and reconstruct only its outer alpha silhouette;
# never carry the checkerboard into the application assets.
image = Image.open(SOURCE).convert("RGB")
if image.size != (1408, 768):
    raise SystemExit(f"Unexpected source dimensions: {image.size}")

crop = image.crop((434, 114, 974, 654))
scale = 4
mask_large = Image.new("L", (crop.width * scale, crop.height * scale), 0)
draw = ImageDraw.Draw(mask_large)
draw.rounded_rectangle(
    (2 * scale, 2 * scale, (crop.width - 2) * scale, (crop.height - 2) * scale),
    radius=112 * scale,
    fill=255,
)
mask = mask_large.resize(crop.size, Image.Resampling.LANCZOS)
art = crop.convert("RGBA")
art.putalpha(mask)

ASSET_DIR.mkdir(parents=True, exist_ok=True)
master = art.resize((512, 512), Image.Resampling.LANCZOS)
master.save(PNG, optimize=True)
master.save(
    ICO,
    format="ICO",
    sizes=[(16, 16), (20, 20), (24, 24), (32, 32), (40, 40), (48, 48), (64, 64), (128, 128), (256, 256)],
    bitmap_format="png",
)

print(f"Wrote {PNG} ({PNG.stat().st_size} bytes)")
print(f"Wrote {ICO} ({ICO.stat().st_size} bytes)")
print(f"Alpha extrema: {master.getchannel('A').getextrema()}")
