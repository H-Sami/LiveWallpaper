import ctypes
import sys
import time

user32 = ctypes.WinDLL("user32", use_last_error=True)
hwnd = user32.FindWindowW(None, "LiveWallpaper")
if not hwnd:
    raise SystemExit("LiveWallpaper window not found")
x = int(sys.argv[1])
y = int(sys.argv[2])
lparam = (y << 16) | (x & 0xFFFF)
WM_MOUSEMOVE = 0x0200
WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
MK_LBUTTON = 0x0001
user32.PostMessageW(hwnd, WM_MOUSEMOVE, 0, lparam)
user32.PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lparam)
time.sleep(0.08)
user32.PostMessageW(hwnd, WM_LBUTTONUP, 0, lparam)
print(f"clicked {x},{y} in hwnd {hwnd}")
