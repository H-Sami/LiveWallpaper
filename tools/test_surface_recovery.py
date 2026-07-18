import ctypes
import time
from ctypes import wintypes

user32 = ctypes.WinDLL("user32", use_last_error=True)
EnumProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)


def class_name(hwnd):
    buffer = ctypes.create_unicode_buffer(128)
    user32.GetClassNameW(hwnd, buffer, len(buffer))
    return buffer.value


def find_surface():
    found = None

    @EnumProc
    def child_callback(hwnd, _):
        nonlocal found
        if class_name(hwnd) == "LiveWallpaper.Host.Surface":
            found = hwnd
            return False
        return True

    @EnumProc
    def top_callback(hwnd, _):
        nonlocal found
        if class_name(hwnd) == "LiveWallpaper.Host.Surface":
            found = hwnd
            return False
        user32.EnumChildWindows(hwnd, child_callback, 0)
        return found is None

    user32.EnumWindows(top_callback, 0)
    return found


old_surface = find_surface()
if not old_surface:
    raise SystemExit("wallpaper surface not found")
if not user32.PostMessageW(old_surface, 0x0010, 0, 0):  # WM_CLOSE
    raise ctypes.WinError(ctypes.get_last_error())
print(f"requested destruction of surface {int(old_surface)}")

deadline = time.monotonic() + 8.0
new_surface = None
while time.monotonic() < deadline:
    candidate = find_surface()
    if candidate and candidate != old_surface:
        parent = user32.GetParent(candidate)
        if class_name(parent) in {"WorkerW", "Progman"}:
            new_surface = candidate
            break
    time.sleep(0.2)
if not new_surface:
    raise SystemExit("host did not recreate and reattach the wallpaper surface")
print(f"recreated surface {int(new_surface)} under {class_name(user32.GetParent(new_surface))}")
