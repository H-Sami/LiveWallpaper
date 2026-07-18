import ctypes
import subprocess
import threading
import time
import urllib.parse
from ctypes import wintypes
from pathlib import Path

from PIL import ImageChops, ImageGrab

ROOT = Path(__file__).resolve().parents[1]
HOST_EXE = ROOT / "build" / "LiveWallpaper.Host.exe"
FIXTURE = ROOT / "tests" / "fixtures" / "test-1080p.mp4"
DATA = Path.home() / "AppData" / "Local" / "LiveWallpaper"
SETTINGS = DATA / "settings.ini"
STATUS = DATA / "runtime.status"

WM_CLOSE = 0x0010
WM_DISPLAYCHANGE = 0x007E
WM_POWERBROADCAST = 0x0218
WM_WTSSESSION_CHANGE = 0x02B1
WTS_SESSION_LOCK = 7
WTS_SESSION_UNLOCK = 8
PBT_APMSUSPEND = 4
PBT_APMRESUMEAUTOMATIC = 18
SM_XVIRTUALSCREEN = 76
SM_YVIRTUALSCREEN = 77
SM_CXVIRTUALSCREEN = 78
SM_CYVIRTUALSCREEN = 79

user32 = ctypes.WinDLL("user32", use_last_error=True)
EnumProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
WndProc = ctypes.WINFUNCTYPE(wintypes.LPARAM, wintypes.HWND, wintypes.UINT,
                            wintypes.WPARAM, wintypes.LPARAM)


class WndClass(ctypes.Structure):
    _fields_ = [
        ("style", wintypes.UINT), ("lpfnWndProc", WndProc), ("cbClsExtra", ctypes.c_int),
        ("cbWndExtra", ctypes.c_int), ("hInstance", wintypes.HINSTANCE),
        ("hIcon", wintypes.HANDLE), ("hCursor", wintypes.HANDLE),
        ("hbrBackground", wintypes.HANDLE), ("lpszMenuName", wintypes.LPCWSTR),
        ("lpszClassName", wintypes.LPCWSTR),
    ]
user32.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.SendMessageW.restype = wintypes.LPARAM
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.PostMessageW.restype = wintypes.BOOL
user32.GetParent.argtypes = [wintypes.HWND]
user32.GetParent.restype = wintypes.HWND
user32.SetParent.argtypes = [wintypes.HWND, wintypes.HWND]
user32.SetParent.restype = wintypes.HWND
user32.CreateWindowExW.restype = wintypes.HWND
user32.DefWindowProcW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.DefWindowProcW.restype = wintypes.LPARAM


@WndProc
def fake_worker_proc(hwnd, message, wparam, lparam):
    if message == WM_CLOSE:
        user32.DestroyWindow(hwnd)
        return 0
    if message == 0x0002:  # WM_DESTROY
        user32.PostQuitMessage(0)
        return 0
    return user32.DefWindowProcW(hwnd, message, wparam, lparam)


def create_fake_worker():
    ready = threading.Event()
    state = {}

    def window_thread():
        instance = ctypes.windll.kernel32.GetModuleHandleW(None)
        window_class = WndClass()
        window_class.lpfnWndProc = fake_worker_proc
        window_class.hInstance = instance
        window_class.lpszClassName = "WorkerW"
        atom = user32.RegisterClassW(ctypes.byref(window_class))
        if not atom and ctypes.get_last_error() not in {0, 1410}:
            state["error"] = ctypes.WinError(ctypes.get_last_error())
            ready.set()
            return
        state["hwnd"] = user32.CreateWindowExW(0, "WorkerW", "Fake WorkerW", 0x00CF0000,
                                                0, 0, 640, 480, None, None, instance, None)
        ready.set()
        message = wintypes.MSG()
        while state["hwnd"] and user32.GetMessageW(ctypes.byref(message), None, 0, 0) > 0:
            user32.TranslateMessage(ctypes.byref(message))
            user32.DispatchMessageW(ctypes.byref(message))

    thread = threading.Thread(target=window_thread, daemon=True)
    thread.start()
    if not ready.wait(5):
        raise TimeoutError("fake WorkerW thread did not start")
    if "error" in state:
        raise state["error"]
    if not state.get("hwnd"):
        raise ctypes.WinError(ctypes.get_last_error())
    return state["hwnd"], thread


def class_name(hwnd):
    value = ctypes.create_unicode_buffer(128)
    user32.GetClassNameW(hwnd, value, len(value))
    return value.value


def process_id(hwnd):
    value = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(value))
    return value.value


def find_window(target):
    found = None

    @EnumProc
    def child(hwnd, _):
        nonlocal found
        if class_name(hwnd) == target:
            found = hwnd
            return False
        return True

    @EnumProc
    def top(hwnd, _):
        nonlocal found
        if class_name(hwnd) == target:
            found = hwnd
            return False
        user32.EnumChildWindows(hwnd, child, 0)
        return found is None

    user32.EnumWindows(top, 0)
    return found


def parse_status():
    values = {}
    try:
        lines = STATUS.read_text(encoding="utf-8").splitlines()
    except (FileNotFoundError, PermissionError):
        return values
    for line in lines:
        key, separator, value = line.partition("=")
        if separator:
            values[key] = urllib.parse.unquote(value)
    return values


def wait_playing(timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        status = parse_status()
        if status.get("state") == "playing":
            return
        if status.get("state") == "error":
            raise RuntimeError(status)
        time.sleep(0.1)
    raise TimeoutError(f"host did not report playing: {parse_status()}")


def wait_surface(previous=None, timeout=12):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        surface = find_window("LiveWallpaper.Host.Surface")
        if surface and surface != previous:
            parent = user32.GetParent(surface)
            if class_name(parent) in {"WorkerW", "Progman"}:
                return surface
        time.sleep(0.15)
    raise TimeoutError("wallpaper surface did not recover")


def assert_geometry(surface):
    rect = wintypes.RECT()
    if not user32.GetWindowRect(surface, ctypes.byref(rect)):
        raise ctypes.WinError(ctypes.get_last_error())
    actual = (rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top)
    expected = (
        user32.GetSystemMetrics(SM_XVIRTUALSCREEN),
        user32.GetSystemMetrics(SM_YVIRTUALSCREEN),
        user32.GetSystemMetrics(SM_CXVIRTUALSCREEN),
        user32.GetSystemMetrics(SM_CYVIRTUALSCREEN),
    )
    if actual != expected:
        raise AssertionError(f"surface geometry {actual} != {expected}")


def changed_ratio(delay=1.0):
    first = ImageGrab.grab(all_screens=True).convert("RGB")
    time.sleep(delay)
    second = ImageGrab.grab(all_screens=True).convert("RGB")
    difference = ImageChops.difference(first, second).convert("L")
    histogram = difference.histogram()
    return sum(histogram[6:]) / (first.width * first.height)


def assert_moving(label):
    ratio = max(changed_ratio(0.5) for _ in range(3))
    if ratio < 0.004:
        raise AssertionError(f"{label} did not visibly move: {ratio:.6f}")
    print(f"{label}: moving ratio={ratio:.6f}")


def assert_paused(label):
    time.sleep(1.0)  # Allow asynchronous Media Foundation pause to drain queued presentation.
    ratio = changed_ratio(2.0)
    if ratio > 0.001:
        raise AssertionError(f"{label} continued moving: {ratio:.6f}")
    print(f"{label}: paused ratio={ratio:.6f}")


def stop_host():
    subprocess.run([str(HOST_EXE), "--exit"], timeout=8, check=False)
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        if not find_window("LiveWallpaper.Host.Window"):
            return
        time.sleep(0.1)
    raise TimeoutError("wallpaper host did not exit")


original = SETTINGS.read_bytes() if SETTINGS.exists() else None
process = None
try:
    stop_host()
    if not find_window("Progman"):
        subprocess.Popen(["explorer.exe"], creationflags=0x08000000)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline and not find_window("Progman"):
            time.sleep(0.2)
        if not find_window("Progman"):
            raise TimeoutError("Explorer desktop did not become ready")
    DATA.mkdir(parents=True, exist_ok=True)
    SETTINGS.write_text(
        f"version=1\nmedia={FIXTURE}\nmuted=1\nvolume=0\nstart_ms=0\nend_ms=0\n"
        "close_to_tray=1\nplaying=1\n",
        encoding="utf-8",
    )
    STATUS.unlink(missing_ok=True)
    subprocess.run(
        ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command",
         "(New-Object -ComObject Shell.Application).MinimizeAll()"],
        timeout=10, check=False, creationflags=0x08000000,
    )
    process = subprocess.Popen([str(HOST_EXE), "--apply"])
    wait_playing()
    host = find_window("LiveWallpaper.Host.Window")
    if not host:
        raise RuntimeError("host command window was not found")
    surface = wait_surface()
    assert_geometry(surface)
    assert_moving("initial playback")

    old_parent = user32.GetParent(surface)
    ctypes.set_last_error(0)
    previous = user32.SetParent(surface, None)
    if not previous and ctypes.get_last_error():
        raise ctypes.WinError(ctypes.get_last_error())
    time.sleep(3)
    if class_name(user32.GetParent(surface)) not in {"WorkerW", "Progman"}:
        raise AssertionError("forced detach was not recovered")
    assert_moving("forced-detach recovery")

    fake_worker, fake_worker_thread = create_fake_worker()
    try:
        user32.SetParent(surface, fake_worker)
        deadline = time.monotonic() + 4.0
        while time.monotonic() < deadline and user32.GetParent(surface) == fake_worker:
            time.sleep(0.1)
        if user32.GetParent(surface) == fake_worker:
            raise AssertionError("host accepted an unrelated process's fake WorkerW")
        if class_name(user32.GetParent(surface)) not in {"WorkerW", "Progman"}:
            raise AssertionError("surface did not return to the Explorer shell")
    finally:
        user32.PostMessageW(fake_worker, WM_CLOSE, 0, 0)
        fake_worker_thread.join(timeout=3)
    assert_moving("shell-identity recovery")

    old_surface = surface
    if not user32.PostMessageW(old_surface, WM_CLOSE, 0, 0):
        raise ctypes.WinError(ctypes.get_last_error())
    surface = wait_surface(previous=old_surface, timeout=1.0)
    assert_geometry(surface)
    assert_moving("surface-destruction recovery")

    user32.SendMessageW(host, WM_DISPLAYCHANGE, 32, 0)
    time.sleep(0.5)
    assert_geometry(surface)
    assert_moving("display-change recovery")

    user32.SendMessageW(host, WM_WTSSESSION_CHANGE, WTS_SESSION_LOCK, 0)
    assert_paused("session-lock")
    user32.SendMessageW(host, WM_WTSSESSION_CHANGE, WTS_SESSION_UNLOCK, 0)
    assert_moving("session-unlock")

    user32.SendMessageW(host, WM_POWERBROADCAST, PBT_APMSUSPEND, 0)
    assert_paused("power-suspend")
    user32.SendMessageW(host, WM_POWERBROADCAST, PBT_APMRESUMEAUTOMATIC, 0)
    assert_moving("power-resume")

    explorer_surface = surface
    old_explorer_pid = process_id(find_window("Progman"))
    subprocess.run(["taskkill.exe", "/F", "/IM", "explorer.exe"], timeout=15, check=False,
                   creationflags=0x08000000)
    time.sleep(1.0)
    if not find_window("Progman"):
        subprocess.Popen(["explorer.exe"], creationflags=0x08000000)
    surface = wait_surface(timeout=20)
    new_explorer_pid = process_id(user32.GetParent(surface))
    if not new_explorer_pid or new_explorer_pid == old_explorer_pid:
        raise AssertionError(
            f"surface did not attach to a replacement Explorer process: {old_explorer_pid} -> {new_explorer_pid}"
        )
    assert_geometry(surface)
    assert_moving("Explorer-restart recovery")

    print("Recovery matrix passed")
finally:
    try:
        stop_host()
    except Exception:
        pass
    if not find_window("Progman"):
        subprocess.Popen(["explorer.exe"], creationflags=0x08000000)
        time.sleep(2.0)
    if original is None:
        SETTINGS.unlink(missing_ok=True)
    else:
        SETTINGS.write_bytes(original)
    subprocess.run(
        ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command",
         "(New-Object -ComObject Shell.Application).UndoMinimizeAll()"],
        timeout=10, check=False, creationflags=0x08000000,
    )
