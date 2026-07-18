import ctypes
import subprocess
import time
import urllib.parse
from ctypes import wintypes
from pathlib import Path

from PIL import ImageChops, ImageGrab, ImageStat

ROOT = Path(__file__).resolve().parents[1]
HOST = ROOT / "build" / "LiveWallpaper.Host.exe"
DATA = Path.home() / "AppData" / "Local" / "LiveWallpaper"
SETTINGS = DATA / "settings.ini"
STATUS = DATA / "runtime.status"
FIXTURES = [
    ("1080p", ROOT / "tests" / "fixtures" / "test-1080p.mp4", (1920, 1080)),
    ("4k", ROOT / "tests" / "fixtures" / "test-4k.mp4", (3840, 2160)),
    ("h264-mkv", ROOT / "tests" / "fixtures" / "test-h264.mkv", (1920, 1080)),
    ("h264-mov", ROOT / "tests" / "fixtures" / "test-h264.mov", (1920, 1080)),
    ("vp9-webm", ROOT / "tests" / "fixtures" / "test-vp9.webm", (640, 360)),
    ("portrait", ROOT / "tests" / "fixtures" / "test-portrait.mp4", (1080, 1920)),
    ("ultrawide", ROOT / "tests" / "fixtures" / "test-ultrawide.mp4", (3440, 1440)),
    ("4x3", ROOT / "tests" / "fixtures" / "test-4x3.mp4", (1440, 1080)),
]

user32 = ctypes.WinDLL("user32", use_last_error=True)
EnumProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)


def class_name(hwnd):
    value = ctypes.create_unicode_buffer(128)
    user32.GetClassNameW(hwnd, value, len(value))
    return value.value


def find_surface():
    found = None

    @EnumProc
    def child(hwnd, _):
        nonlocal found
        if class_name(hwnd) == "LiveWallpaper.Host.Surface":
            found = hwnd
            return False
        return True

    @EnumProc
    def top(hwnd, _):
        nonlocal found
        if class_name(hwnd) == "LiveWallpaper.Host.Surface":
            found = hwnd
            return False
        user32.EnumChildWindows(hwnd, child, 0)
        return found is None

    user32.EnumWindows(top, 0)
    return found


def wait_for_state(expected, timeout=12):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            payload = STATUS.read_text(encoding="utf-8")
            status = {}
            for line in payload.splitlines():
                key, separator, value = line.partition("=")
                if separator:
                    status[key] = urllib.parse.unquote(value)
            state = status.get("state", "")
        except (FileNotFoundError, PermissionError):
            state = ""
        if state == expected:
            return
        if state == "error":
            raise RuntimeError(f"host reported a media or scaling error: {status}")
        time.sleep(0.075)
    raise TimeoutError(f"host did not report state={expected}")


def write_fixture_settings(path):
    SETTINGS.write_text(
        "version=1\n"
        f"media={path}\n"
        "muted=1\nvolume=0\nstart_ms=0\nend_ms=0\n"
        "close_to_tray=1\nplaying=1\n",
        encoding="utf-8",
    )


def verify_surface_geometry():
    surface = find_surface()
    if not surface:
        raise RuntimeError("wallpaper surface was not found")
    parent_class = class_name(user32.GetParent(surface))
    if parent_class not in {"WorkerW", "Progman"}:
        raise RuntimeError(f"surface parent is {parent_class}, not WorkerW/Progman")
    rect = wintypes.RECT()
    if not user32.GetWindowRect(surface, ctypes.byref(rect)):
        raise ctypes.WinError(ctypes.get_last_error())
    actual = (rect.left, rect.top, rect.right, rect.bottom)
    expected = (
        user32.GetSystemMetrics(76),  # SM_XVIRTUALSCREEN
        user32.GetSystemMetrics(77),  # SM_YVIRTUALSCREEN
        user32.GetSystemMetrics(76) + user32.GetSystemMetrics(78),
        user32.GetSystemMetrics(77) + user32.GetSystemMetrics(79),
    )
    if actual != expected:
        raise RuntimeError(f"surface rect {actual} does not match virtual desktop {expected}")
    return surface, actual, parent_class


def capture_motion(label):
    first = ImageGrab.grab(all_screens=True).convert("RGB")
    second = first
    difference = ImageChops.difference(first, second)
    mean = 0.0
    ratio = 0.0
    for _ in range(10):
        time.sleep(0.35)
        candidate = ImageGrab.grab(all_screens=True).convert("RGB")
        candidate_difference = ImageChops.difference(first, candidate)
        candidate_mean = sum(ImageStat.Stat(candidate_difference).mean) / 3.0
        histogram = candidate_difference.convert("L").histogram()
        candidate_ratio = sum(histogram[11:]) / (first.width * first.height)
        if candidate_ratio > ratio:
            second = candidate
            difference = candidate_difference
            mean = candidate_mean
            ratio = candidate_ratio
        if ratio >= 0.005:
            break
    first.save(ROOT / "build" / f"scaling-{label}-a.png")
    second.save(ROOT / "build" / f"scaling-{label}-b.png")
    difference.save(ROOT / "build" / f"scaling-{label}-diff.png")
    if ratio < 0.005:
        raise RuntimeError(f"insufficient visible motion for {label}: {ratio:.6f}")
    return mean, ratio


def stop_existing_host():
    subprocess.run([str(HOST), "--exit"], timeout=8, check=False)
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if not find_surface():
            return
        time.sleep(0.1)


original = SETTINGS.read_bytes() if SETTINGS.exists() else None
DATA.mkdir(parents=True, exist_ok=True)
process = None
try:
    stop_existing_host()
    subprocess.run(
        ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command",
         "(New-Object -ComObject Shell.Application).MinimizeAll()"],
        timeout=10, check=False, creationflags=0x08000000,
    )
    for index, (label, fixture, expected_resolution) in enumerate(FIXTURES):
        if not fixture.exists():
            raise FileNotFoundError(fixture)
        write_fixture_settings(fixture)
        STATUS.unlink(missing_ok=True)
        if index == 0:
            process = subprocess.Popen([str(HOST), "--apply"])
        else:
            command = subprocess.run([str(HOST), "--apply"], timeout=12)
            if command.returncode != 0:
                raise RuntimeError(f"relay apply failed with code {command.returncode}")
        wait_for_state("playing")
        surface, rect, parent = verify_surface_geometry()
        time.sleep(0.75)  # Allow the initial asynchronous seek and first EVR frame to present.
        mean, ratio = capture_motion(label)
        print(
            f"{label}: source={expected_resolution[0]}x{expected_resolution[1]} "
            f"surface={rect[2]-rect[0]}x{rect[3]-rect[1]} parent={parent} "
            f"motion_mean={mean:.4f} changed_ratio={ratio:.6f}"
        )
finally:
    subprocess.run([str(HOST), "--exit"], timeout=8, check=False)
    if original is None:
        SETTINGS.unlink(missing_ok=True)
    else:
        SETTINGS.write_bytes(original)
    subprocess.run(
        ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command",
         "(New-Object -ComObject Shell.Application).UndoMinimizeAll()"],
        timeout=10, check=False, creationflags=0x08000000,
    )
