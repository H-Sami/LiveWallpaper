#pragma once

#include <Windows.h>

#include <string>

namespace livewallpaper {

// Keeps all undocumented shell interaction in one recoverable component.
class WallpaperHost {
public:
    bool Attach(HWND wallpaper, std::wstring& error);
    bool IsAttached(HWND wallpaper) const;
    void SizeToVirtualDesktop(HWND wallpaper) const;

private:
    HWND FindWorkerW(std::wstring& error) const;
    bool IsValidExplorerHost(HWND host, DWORD explorerPid) const;

    HWND selectedHost_{};
    DWORD explorerPid_{};
};

} // namespace livewallpaper
