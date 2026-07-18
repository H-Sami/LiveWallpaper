import subprocess
import time
import urllib.parse
import uuid
from pathlib import Path

from PIL import ImageChops, ImageGrab
from comtypes import CLSCTX_ALL, POINTER, cast
from pycaw.pycaw import AudioUtilities, IAudioMeterInformation

ROOT = Path(__file__).resolve().parents[1]
HOST = ROOT / "build" / "LiveWallpaper.Host.exe"
AUDIO_VIDEO = ROOT / "tests" / "fixtures" / "test-audio.mp4"
INVALID_VIDEO = ROOT / "tests" / "fixtures" / "test-invalid.mp4"
DATA = Path.home() / "AppData" / "Local" / "LiveWallpaper"
SETTINGS = DATA / "settings.ini"
STATUS = DATA / "runtime.status"


def request_id():
    return uuid.uuid4().hex


def status_path(identifier):
    return Path(f"{STATUS}.{identifier}")


def parse(path):
    values = {}
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except (FileNotFoundError, PermissionError):
        return values
    for line in lines:
        key, separator, value = line.partition("=")
        if separator:
            values[key] = urllib.parse.unquote(value)
    return values


def wait_terminal(identifier, timeout=15):
    path = status_path(identifier)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        status = parse(path)
        if status.get("request_id") == identifier and status.get("state") in {
            "playing", "error", "stopped", "superseded"
        }:
            return status
        time.sleep(0.075)
    raise TimeoutError(f"request {identifier} did not finish: {parse(path)}")


def settings(path, muted, volume, start_ms, end_ms):
    SETTINGS.write_text(
        f"version=1\nmedia={path}\nmuted={1 if muted else 0}\nvolume={volume}\n"
        f"start_ms={start_ms}\nend_ms={end_ms}\nclose_to_tray=1\nplaying=1\n",
        encoding="utf-8",
    )


def apply(process=None):
    identifier = request_id()
    status_path(identifier).unlink(missing_ok=True)
    if process is None:
        command = subprocess.run([str(HOST), f"--apply|{identifier}"], timeout=12)
        if command.returncode not in {0, 3}:
            raise RuntimeError(f"Apply relay failed with {command.returncode}")
    else:
        process = subprocess.Popen([str(HOST), f"--apply|{identifier}"])
    return process, wait_terminal(identifier)


def peak_level(seconds=1.25):
    device = AudioUtilities.GetSpeakers()
    interface = device._dev.Activate(IAudioMeterInformation._iid_, CLSCTX_ALL, None)
    meter = cast(interface, POINTER(IAudioMeterInformation))
    deadline = time.monotonic() + seconds
    peak = 0.0
    while time.monotonic() < deadline:
        peak = max(peak, meter.GetPeakValue())
        time.sleep(0.025)
    return peak


def changed_ratio(delay):
    first = ImageGrab.grab(all_screens=True).convert("RGB")
    time.sleep(delay)
    second = ImageGrab.grab(all_screens=True).convert("RGB")
    histogram = ImageChops.difference(first, second).convert("L").histogram()
    return sum(histogram[6:]) / (first.width * first.height)


def assert_repeated_motion(label, samples, delay=0.45):
    ratios = [changed_ratio(delay) for _ in range(samples)]
    if sum(ratio >= 0.003 for ratio in ratios) < max(1, samples - 1):
        raise AssertionError(f"{label} stalled: {ratios}")
    print(f"{label}: min={min(ratios):.6f} max={max(ratios):.6f}")


DATA.mkdir(parents=True, exist_ok=True)
original = SETTINGS.read_bytes() if SETTINGS.exists() else None
host_process = None
created_statuses = []
try:
    subprocess.run([str(HOST), "--exit"], timeout=8, check=False)
    subprocess.run(
        ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command",
         "(New-Object -ComObject Shell.Application).MinimizeAll()"],
        timeout=10, check=False, creationflags=0x08000000,
    )

    settings(AUDIO_VIDEO, False, 100, 1000, 3000)
    host_process, result = apply(process=True)
    assert result["state"] == "playing", result
    full_peak = peak_level()
    assert full_peak > 0.01, full_peak
    assert_repeated_motion("trimmed-loop playback", 8)

    settings(AUDIO_VIDEO, False, 37, 1000, 3000)
    _, result = apply()
    assert result["state"] == "playing", result
    reduced_peak = peak_level()
    assert full_peak * 0.15 < reduced_peak < full_peak * 0.65, (full_peak, reduced_peak)

    settings(AUDIO_VIDEO, True, 82, 1000, 3000)
    _, result = apply()
    assert result["state"] == "playing", result
    muted_peak = peak_level()
    assert muted_peak < max(0.005, full_peak * 0.08), (full_peak, muted_peak)
    print(f"audio controls: full={full_peak:.4f} reduced={reduced_peak:.4f} muted={muted_peak:.4f}")

    settings(AUDIO_VIDEO, True, 0, 2000, 2000)
    _, result = apply()
    assert result["state"] == "error", result
    assert result["error_code"].startswith("media-ready-"), result

    settings(INVALID_VIDEO, True, 0, 0, 0)
    _, result = apply()
    assert result["state"] == "error", result
    assert result["error_code"] in {"media-open-failed", "media-playback-failed"}, result
    print(f"invalid-media: {result['error_code']}")

    settings(AUDIO_VIDEO, True, 0, 0, 0)
    _, result = apply()
    assert result["state"] == "playing", result
    time.sleep(6.5)
    assert_repeated_motion("end-of-media replay", 4)

    print("Video behavior matrix passed")
finally:
    subprocess.run([str(HOST), "--exit"], timeout=8, check=False)
    if original is None:
        SETTINGS.unlink(missing_ok=True)
    else:
        SETTINGS.write_bytes(original)
    for path in DATA.glob("runtime.status.*"):
        if path.is_file():
            try:
                path.unlink()
            except OSError:
                pass
    subprocess.run(
        ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command",
         "(New-Object -ComObject Shell.Application).UndoMinimizeAll()"],
        timeout=10, check=False, creationflags=0x08000000,
    )
