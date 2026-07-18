import ctypes
import time
from pathlib import Path
from PIL import ImageGrab

user32 = ctypes.WinDLL("user32", use_last_error=True)
hwnd = user32.FindWindowW(None, "LiveWallpaper")
if not hwnd:
    raise SystemExit("LiveWallpaper window not found")
user32.ShowWindow(hwnd, 9)  # SW_RESTORE
user32.SetForegroundWindow(hwnd)
time.sleep(0.8)
rect = ctypes.wintypes.RECT() if hasattr(ctypes, "wintypes") else None
if rect is None:
    from ctypes import wintypes
    rect = wintypes.RECT()
user32.GetWindowRect(hwnd, ctypes.byref(rect))
path = Path(__file__).resolve().parents[1] / "build" / "controller-window.png"
ImageGrab.grab(bbox=(rect.left, rect.top, rect.right, rect.bottom)).save(path)
print(path)
