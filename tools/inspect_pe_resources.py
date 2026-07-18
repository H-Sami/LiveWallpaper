import ctypes
import sys
from ctypes import wintypes

path = sys.argv[1]
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
LOAD_LIBRARY_AS_DATAFILE = 0x00000002
kernel32.LoadLibraryExW.argtypes = [wintypes.LPCWSTR, wintypes.HANDLE, wintypes.DWORD]
kernel32.LoadLibraryExW.restype = wintypes.HMODULE
module = kernel32.LoadLibraryExW(path, None, LOAD_LIBRARY_AS_DATAFILE)
if not module:
    raise ctypes.WinError(ctypes.get_last_error())

EnumProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HMODULE, ctypes.c_void_p, ctypes.c_void_p, wintypes.LPARAM)
kernel32.EnumResourceNamesW.argtypes = [wintypes.HMODULE, ctypes.c_void_p, EnumProc, wintypes.LPARAM]
kernel32.EnumResourceNamesW.restype = wintypes.BOOL
kernel32.FreeLibrary.argtypes = [wintypes.HMODULE]
kernel32.FreeLibrary.restype = wintypes.BOOL


def count_resources(resource_type):
    count = 0

    @EnumProc
    def callback(_module, _type, _name, _parameter):
        nonlocal count
        count += 1
        return True

    kernel32.EnumResourceNamesW(module, ctypes.c_void_p(resource_type), callback, 0)
    error = ctypes.get_last_error()
    if count == 0 and error not in (0, 1813):
        raise ctypes.WinError(error)
    return count


try:
    print(f"icon_count={count_resources(3)}")  # RT_ICON
    print(f"version_resource_count={count_resources(16)}")  # RT_VERSION
finally:
    kernel32.FreeLibrary(module)
