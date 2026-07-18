#include "wallpaper_host.h"

#include <cstdint>
#include <iterator>

namespace livewallpaper {
namespace {
constexpr UINT kSpawnWorkerMessage = 0x052C;
constexpr UINT kShellTimeoutMs = 1500;

bool CoversVirtualDesktop(HWND window) {
    RECT bounds{};
    if (!GetWindowRect(window, &bounds)) return false;
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int right = left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int bottom = top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return bounds.left <= left && bounds.top <= top && bounds.right >= right && bounds.bottom >= bottom;
}

struct SearchState { HWND host = nullptr; DWORD explorerPid{}; };

BOOL CALLBACK FindShellView(HWND top, LPARAM value) {
    auto* state = reinterpret_cast<SearchState*>(value);
    if (!FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr)) return TRUE;
    // The wallpaper WorkerW is normally the next top-level WorkerW after the
    // window that owns SHELLDLL_DefView. On older Explorer builds Progman itself
    // owns the view, and parenting to Progman is the compatible fallback.
    HWND candidate = FindWindowExW(nullptr, top, L"WorkerW", nullptr);
    if (!candidate) candidate = top;
    DWORD processId = 0;
    GetWindowThreadProcessId(candidate, &processId);
    if (processId == state->explorerPid && CoversVirtualDesktop(candidate)) {
        state->host = candidate;
        return FALSE;
    }
    return TRUE;
}
}

HWND WallpaperHost::FindWorkerW(std::wstring& error) const {
    const HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman) { error = L"Explorer desktop host (Progman) was not found."; return nullptr; }
    DWORD explorerPid = 0;
    GetWindowThreadProcessId(progman, &explorerPid);
    if (!explorerPid) { error = L"Explorer desktop host identity could not be verified."; return nullptr; }

    DWORD_PTR ignored = 0;
    // Both forms are used by supported Windows 10/11 Explorer versions.
    if (!SendMessageTimeoutW(progman, kSpawnWorkerMessage, 0xD, 0,
            SMTO_ABORTIFHUNG | SMTO_BLOCK, kShellTimeoutMs, &ignored)) {
        error = L"Explorer did not respond while creating the wallpaper host.";
        return nullptr;
    }
    SendMessageTimeoutW(progman, kSpawnWorkerMessage, 0xD, 1,
        SMTO_ABORTIFHUNG | SMTO_BLOCK, kShellTimeoutMs, &ignored);

    // Windows 11 may create the wallpaper WorkerW as a child of Progman
    // instead of as a top-level sibling. Prefer the largest such child: it is
    // the surface below SHELLDLL_DefView and above the static wallpaper.
    HWND childWorker = nullptr;
    std::int64_t largestArea = 0;
    for (HWND candidate = FindWindowExW(progman, nullptr, L"WorkerW", nullptr);
         candidate;
         candidate = FindWindowExW(progman, candidate, L"WorkerW", nullptr)) {
        DWORD candidatePid = 0;
        GetWindowThreadProcessId(candidate, &candidatePid);
        if (candidatePid != explorerPid || !CoversVirtualDesktop(candidate)) continue;
        RECT bounds{};
        if (!GetWindowRect(candidate, &bounds)) continue;
        const auto width = static_cast<std::int64_t>(bounds.right) - bounds.left;
        const auto height = static_cast<std::int64_t>(bounds.bottom) - bounds.top;
        const auto area = width > 0 && height > 0 ? width * height : 0;
        if (area > largestArea) {
            childWorker = candidate;
            largestArea = area;
        }
    }
    if (childWorker) return childWorker;

    SearchState search;
    search.explorerPid = explorerPid;
    EnumWindows(FindShellView, reinterpret_cast<LPARAM>(&search));
    if (!search.host) {
        // Recovery for shell variants where enumeration races WorkerW creation.
        Sleep(50);
        EnumWindows(FindShellView, reinterpret_cast<LPARAM>(&search));
    }
    if (!search.host) error = L"Explorer created no usable WorkerW wallpaper host.";
    return search.host;
}

bool WallpaperHost::Attach(HWND wallpaper, std::wstring& error) {
    if (!IsWindow(wallpaper)) { error = L"The wallpaper window is unavailable."; return false; }
    HWND host = FindWorkerW(error);
    if (!host) return false;
    const HWND progman = FindWindowW(L"Progman", nullptr);
    DWORD explorerPid = 0;
    if (progman) GetWindowThreadProcessId(progman, &explorerPid);
    if (!IsValidExplorerHost(host, explorerPid)) {
        error = L"Explorer returned an invalid wallpaper host.";
        return false;
    }
    SetLastError(ERROR_SUCCESS);
    const HWND previous = SetParent(wallpaper, host);
    if (!previous && GetLastError() != ERROR_SUCCESS) {
        error = L"Windows refused to attach the wallpaper window to Explorer.";
        return false;
    }
    LONG_PTR style = GetWindowLongPtrW(wallpaper, GWL_STYLE);
    style &= ~static_cast<LONG_PTR>(WS_POPUP);
    style |= WS_CHILD;
    SetWindowLongPtrW(wallpaper, GWL_STYLE, style);
    SizeToVirtualDesktop(wallpaper);
    ShowWindow(wallpaper, SW_SHOWNOACTIVATE);
    selectedHost_ = host;
    explorerPid_ = explorerPid;
    return true;
}

bool WallpaperHost::IsValidExplorerHost(HWND host, DWORD explorerPid) const {
    if (!host || !IsWindow(host) || !explorerPid || !CoversVirtualDesktop(host)) return false;
    DWORD hostPid = 0;
    GetWindowThreadProcessId(host, &hostPid);
    if (hostPid != explorerPid) return false;
    wchar_t className[32]{};
    if (!GetClassNameW(host, className, static_cast<int>(std::size(className)))) return false;
    return _wcsicmp(className, L"WorkerW") == 0 || _wcsicmp(className, L"Progman") == 0;
}

bool WallpaperHost::IsAttached(HWND wallpaper) const {
    const HWND parent = GetParent(wallpaper);
    if (parent != selectedHost_) return false;
    const HWND progman = FindWindowW(L"Progman", nullptr);
    DWORD currentExplorerPid = 0;
    if (progman) GetWindowThreadProcessId(progman, &currentExplorerPid);
    return currentExplorerPid == explorerPid_ && IsValidExplorerHost(parent, currentExplorerPid);
}

void WallpaperHost::SizeToVirtualDesktop(HWND wallpaper) const {
    if (!IsWindow(wallpaper)) return;
    const int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    POINT origin{x, y};
    if (HWND parent = GetParent(wallpaper)) ScreenToClient(parent, &origin);
    SetWindowPos(wallpaper, HWND_BOTTOM, origin.x, origin.y, width, height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

} // namespace livewallpaper
