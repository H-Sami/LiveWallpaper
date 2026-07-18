#pragma once

#include <Windows.h>

namespace livewallpaper {
constexpr UINT WM_APP_MEDIA_READY = WM_APP + 1;
constexpr UINT WM_APP_MEDIA_ENDED = WM_APP + 2;
constexpr UINT WM_APP_MEDIA_ERROR = WM_APP + 3;
constexpr UINT WM_APP_TRAY = WM_APP + 4;
constexpr UINT WM_APP_SURFACE_LOST = WM_APP + 5;
constexpr UINT WM_APP_MEDIA_POSITION_SET = WM_APP + 6;
}
