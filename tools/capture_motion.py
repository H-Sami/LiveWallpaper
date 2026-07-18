import time
from pathlib import Path
from PIL import Image, ImageChops, ImageGrab, ImageStat

out = Path(__file__).resolve().parents[1] / "build"
first = ImageGrab.grab(all_screens=True).convert("RGB")
time.sleep(0.75)
second = ImageGrab.grab(all_screens=True).convert("RGB")
first.save(out / "desktop-frame-a.png")
second.save(out / "desktop-frame-b.png")
difference = ImageChops.difference(first, second)
difference.save(out / "desktop-motion-diff.png")
stat = ImageStat.Stat(difference)
mean = sum(stat.mean) / 3.0
gray = difference.convert("L")
histogram = gray.histogram()
changed = sum(histogram[11:])
total = first.width * first.height
ratio = changed / total
print(f"mean_absolute_difference={mean:.4f}")
print(f"changed_pixel_ratio_gt10={ratio:.6f}")
if ratio < 0.01:
    raise SystemExit("Insufficient desktop motion detected")
