import ctypes
import time
from ctypes import wintypes

user32 = ctypes.WinDLL("user32", use_last_error=True)
EnumProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
user32.GetParent.restype = wintypes.HWND
user32.SetParent.restype = wintypes.HWND

surface = None


def class_name(hwnd):
    buffer = ctypes.create_unicode_buffer(128)
    user32.GetClassNameW(hwnd, buffer, len(buffer))
    return buffer.value


@EnumProc
def child_callback(hwnd, _):
    global surface
    if class_name(hwnd) == "LiveWallpaper.Host.Surface":
        surface = hwnd
        return False
    return True


@EnumProc
def top_callback(hwnd, _):
    global surface
    if class_name(hwnd) == "LiveWallpaper.Host.Surface":
        surface = hwnd
        return False
    user32.EnumChildWindows(hwnd, child_callback, 0)
    return surface is None


user32.EnumWindows(top_callback, 0)
if not surface:
    raise SystemExit("wallpaper surface not found")
old_parent = user32.GetParent(surface)
if not old_parent:
    raise SystemExit("surface is already detached")
ctypes.set_last_error(0)
previous = user32.SetParent(surface, None)
if not previous and ctypes.get_last_error():
    raise ctypes.WinError(ctypes.get_last_error())
print(f"detached surface {int(surface)} from {int(old_parent)}")
time.sleep(3.0)
new_parent = user32.GetParent(surface)
if not new_parent:
    raise SystemExit("host did not reattach the surface")
name = class_name(new_parent)
if name not in {"WorkerW", "Progman"}:
    raise SystemExit(f"surface reattached to unexpected parent {name}")
print(f"reattached to {name} {int(new_parent)}")
