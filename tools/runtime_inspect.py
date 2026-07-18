import ctypes
import json
import sys
from ctypes import wintypes

user32 = ctypes.WinDLL("user32", use_last_error=True)
EnumProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)


def class_name(hwnd):
    buf = ctypes.create_unicode_buffer(256)
    user32.GetClassNameW(hwnd, buf, len(buf))
    return buf.value


def title(hwnd):
    length = user32.GetWindowTextLengthW(hwnd)
    buf = ctypes.create_unicode_buffer(length + 1)
    user32.GetWindowTextW(hwnd, buf, length + 1)
    return buf.value


windows = []
seen = set()


def record(hwnd):
    value = int(hwnd)
    if value in seen:
        return
    seen.add(value)
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    parent = user32.GetParent(hwnd)
    rect = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    windows.append({
        "hwnd": value,
        "class": class_name(hwnd),
        "title": title(hwnd),
        "pid": pid.value,
        "visible": bool(user32.IsWindowVisible(hwnd)),
        "parent": int(parent) if parent else 0,
        "parent_class": class_name(parent) if parent else "",
        "rect": [rect.left, rect.top, rect.right, rect.bottom],
    })


@EnumProc
def child_callback(hwnd, _):
    record(hwnd)
    return True


@EnumProc
def top_callback(hwnd, _):
    record(hwnd)
    user32.EnumChildWindows(hwnd, child_callback, 0)
    return True


user32.EnumWindows(top_callback, 0)
settings = [w for w in windows if w["class"] == "LiveWallpaper.Settings"]
controllers = [w for w in windows if w["title"] == "LiveWallpaper"]
hosts = [w for w in windows if w["class"] == "LiveWallpaper.Host.Window"]
surfaces = [w for w in windows if w["class"] in {"LiveWallpaper.Surface", "LiveWallpaper.Host.Surface"}]
result = {
    "settings_windows": settings,
    "controller_windows": controllers,
    "wpf_windows": [w for w in windows if w["class"].startswith("HwndWrapper")],
    "host_windows": hosts,
    "wallpaper_surfaces": surfaces,
    "shell_windows": [w for w in windows if w["class"] in {"Progman", "WorkerW", "SHELLDLL_DefView", "SysListView32"}],
    "surface_attached_to_shell": any(w["parent_class"] in {"WorkerW", "Progman"} for w in surfaces),
}
print(json.dumps(result, indent=2))
sys.exit(0)
