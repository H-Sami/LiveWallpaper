#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Shellapi.h>
#include <ShlObj.h>
#include <Shlwapi.h>
#include <mfapi.h>
#include <wtsapi32.h>

#include "app_messages.h"
#include "core.h"
#include "mp4_player.h"
#include "resource.h"
#include "wallpaper_host.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

namespace {
using namespace livewallpaper;

constexpr wchar_t kHostClass[] = L"LiveWallpaper.Host.Window";
constexpr wchar_t kWallpaperClass[] = L"LiveWallpaper.Host.Surface";
constexpr wchar_t kHostMutex[] = L"Local\\LiveWallpaper.Host.SingleInstance";
constexpr wchar_t kReadyEvent[] = L"Local\\LiveWallpaper.Host.Ready";
constexpr ULONG_PTR kCommandMagic = 0x4C575043; // LWPC
constexpr UINT_PTR kMp4Timer = 1;
constexpr UINT_PTR kShellTimer = 3;
constexpr UINT_PTR kApplyTimer = 4;
constexpr UINT kTrayId = 1;
constexpr UINT kCmdOpen = 41001;
constexpr UINT kCmdPause = 41002;
constexpr UINT kCmdMute = 41003;
constexpr UINT kCmdStop = 41004;
constexpr UINT kCmdExit = 41005;

struct HostState {
    HINSTANCE instance{};
    HWND hostWindow{};
    HWND wallpaperWindow{};
    Settings settings{};
    WallpaperHost shell{};
    Mp4Player mp4{};
    std::wstring settingsPath;
    std::wstring statusPath;
    std::wstring requestId;
    std::string operation;
    std::uint64_t mediaGeneration{};
    bool userPaused{};
    bool sessionLocked{};
    bool powerSuspended{};
    bool pendingApply{};
    bool shuttingDown{};
    UINT taskbarCreated{};
};

HostState g;

bool IsEffectivelyPaused() {
    return g.userPaused || g.sessionLocked || g.powerSuspended;
}

std::wstring SettingsPath() {
    PWSTR base = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &base))) {
        return L"settings.ini";
    }
    std::wstring directory(base);
    CoTaskMemFree(base);
    directory += L"\\LiveWallpaper";
    CreateDirectoryW(directory.c_str(), nullptr);
    return directory + L"\\settings.ini";
}

void LoadSettings() {
    g.settings = Settings{};
    std::ifstream stream(g.settingsPath, std::ios::binary);
    if (!stream) return;
    const std::string bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (auto parsed = DeserializeSettings(bytes)) g.settings = std::move(*parsed);
}

bool SaveSettings() {
    const std::string bytes = SerializeSettings(g.settings);
    const std::wstring temporary = g.settingsPath + L".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) return false;
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        stream.flush();
        if (!stream) return false;
    }
    return MoveFileExW(temporary.c_str(), g.settingsPath.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

std::string NarrowAscii(std::wstring_view value) {
    std::string result;
    result.reserve(value.size());
    for (wchar_t character : value) result.push_back(static_cast<char>(character));
    return result;
}

bool WriteStatusFile(const std::wstring& path, const std::string& bytes) {
    const std::wstring temporary = path + L".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) return false;
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        stream.flush();
        if (!stream) return false;
    }
    return MoveFileExW(temporary.c_str(), path.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

bool WriteRuntimeStatus(std::string_view state, std::string_view errorCode = {},
                        std::string_view message = {}) {
    if (g.statusPath.empty()) return false;
    RuntimeStatus status;
    status.requestId = NarrowAscii(g.requestId);
    status.operation = g.operation;
    status.state.assign(state);
    status.errorCode.assign(errorCode);
    status.message.assign(message);
    const std::string bytes = SerializeRuntimeStatus(status);
    const bool latestWritten = WriteStatusFile(g.statusPath, bytes);
    const bool requestWritten = g.requestId.empty() ||
        WriteStatusFile(g.statusPath + L"." + g.requestId, bytes);
    return latestWritten && requestWritten;
}

std::wstring ModuleDirectory() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return L".";
    path.resize(length);
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

void LaunchController() {
    const std::wstring path = ModuleDirectory() + L"\\LiveWallpaper.exe";
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void UpdateTray(bool add) {
    if (!g.hostWindow) return;
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = g.hostWindow;
    data.uID = kTrayId;
    if (!add) {
        Shell_NotifyIconW(NIM_DELETE, &data);
        return;
    }
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = WM_APP_TRAY;
    data.hIcon = LoadIconW(g.instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    if (!data.hIcon) data.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    const wchar_t* state = g.settings.playing ? (IsEffectivelyPaused() ? L"LiveWallpaper — Paused" : L"LiveWallpaper — Playing")
                                                 : L"LiveWallpaper — Stopped";
    wcscpy_s(data.szTip, state);
    Shell_NotifyIconW(NIM_ADD, &data);
    data.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data);
}

void RefreshTray() {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = g.hostWindow;
    data.uID = kTrayId;
    data.uFlags = NIF_TIP;
    const wchar_t* state = g.settings.playing ? (IsEffectivelyPaused() ? L"LiveWallpaper — Paused" : L"LiveWallpaper — Playing")
                                                 : L"LiveWallpaper — Stopped";
    wcscpy_s(data.szTip, state);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

void HideWallpaper() {
    if (IsWindow(g.wallpaperWindow)) ShowWindow(g.wallpaperWindow, SW_HIDE);
}

void ResetPlayers() {
    KillTimer(g.hostWindow, kMp4Timer);
    g.mp4.Close();
}

void StopPlayback(bool persist) {
    KillTimer(g.hostWindow, kApplyTimer);
    ResetPlayers();
    g.userPaused = false;
    g.pendingApply = false;
    HideWallpaper();
    g.settings.playing = false;
    if (persist) SaveSettings();
    RefreshTray();
}

bool EnsureWallpaperSurface() {
    if (IsWindow(g.wallpaperWindow)) return true;
    ResetPlayers();
    g.wallpaperWindow = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        kWallpaperClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, g.instance, nullptr);
    return IsWindow(g.wallpaperWindow) != FALSE;
}

bool AttachWallpaper() {
    if (!EnsureWallpaperSurface()) return false;
    std::wstring error;
    if (!g.shell.Attach(g.wallpaperWindow, error)) return false;
    ShowWindow(g.wallpaperWindow, SW_SHOWNOACTIVATE);
    return true;
}

HRESULT ApplyAudio() {
    return g.mp4.SetAudio(g.settings.muted || IsEffectivelyPaused(), g.settings.volume);
}

void ApplyPauseState() {
    if (!g.settings.playing) return;
    const bool paused = IsEffectivelyPaused();
    const HRESULT playbackResult = paused ? g.mp4.Pause() : g.mp4.Resume();
    const HRESULT audioResult = ApplyAudio();
    if (FAILED(playbackResult) || FAILED(audioResult)) {
        ++g.mediaGeneration;
        ResetPlayers();
        g.pendingApply = true;
        if (!paused) SetTimer(g.hostWindow, kApplyTimer, 100, nullptr);
    }
    RefreshTray();
}

void SetUserPaused(bool paused) {
    if (paused == g.userPaused) return;
    g.userPaused = paused;
    ApplyPauseState();
}

void SetSessionLocked(bool locked) {
    if (locked == g.sessionLocked) return;
    g.sessionLocked = locked;
    ApplyPauseState();
}

bool ApplyWallpaper(std::wstring_view requestId = {}) {
    if (!requestId.empty() && g.pendingApply && !g.requestId.empty() && g.requestId != requestId) {
        WriteRuntimeStatus("superseded", "newer-request", "A newer Apply request replaced this request.");
        ++g.mediaGeneration; // Invalidate callbacks that raced with player shutdown.
        ResetPlayers();
        g.requestId.assign(requestId);
        g.operation = "apply";
        SetTimer(g.hostWindow, kApplyTimer, 150, nullptr);
        return true;
    }
    if (!requestId.empty()) g.requestId.assign(requestId);
    g.operation = "apply";
    LoadSettings();
    if (!g.settings.playing || g.settings.mediaPath.empty()) {
        StopPlayback(false);
        WriteRuntimeStatus("stopped", "not-requested", "Wallpaper playback is disabled in settings.");
        return false;
    }
    const DWORD attributes = GetFileAttributesW(g.settings.mediaPath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        StopPlayback(true);
        WriteRuntimeStatus("error", "file-not-found", "The selected wallpaper file could not be found.");
        return false;
    }
    const wchar_t* extension = PathFindExtensionW(g.settings.mediaPath.c_str());
    if (!extension || !IsSupportedVideoExtension(extension)) {
        StopPlayback(true);
        WriteRuntimeStatus("error", "unsupported-container", "Choose an MP4, WebM, MKV, or MOV video file.");
        return false;
    }
    ResetPlayers();
    g.pendingApply = true;
    if (!AttachWallpaper()) {
        WriteRuntimeStatus("pending-shell", "shell-unavailable", "Windows Explorer is not ready to host the wallpaper.");
        return false;
    }

    const std::uint64_t generation = ++g.mediaGeneration;
    if (FAILED(g.mp4.Open(g.wallpaperWindow, g.hostWindow, g.settings.mediaPath, generation))) {
        StopPlayback(true);
        WriteRuntimeStatus("error", "media-open-failed", "Windows Media Foundation could not open this container or codec.");
        return false;
    }
    RefreshTray();
    return true;
}

void ShowTrayMenu() {
    POINT point{};
    GetCursorPos(&point);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kCmdOpen, L"Open LiveWallpaper");
    AppendMenuW(menu, MF_STRING | (!g.settings.playing ? MF_GRAYED : 0), kCmdPause,
                g.userPaused ? L"Resume wallpaper" : L"Pause wallpaper");
    AppendMenuW(menu, MF_STRING | (g.settings.muted ? MF_CHECKED : 0), kCmdMute, L"Mute video audio");
    AppendMenuW(menu, MF_STRING | (!g.settings.playing ? MF_GRAYED : 0), kCmdStop, L"Stop wallpaper");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdExit, L"Exit background host");
    SetForegroundWindow(g.hostWindow);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, point.x, point.y, 0, g.hostWindow, nullptr);
    DestroyMenu(menu);
}

bool ProcessCommand(std::wstring_view command) {
    const auto envelope = ParseCommandEnvelope(command.empty() ? L"--apply" : command);
    if (!envelope) return false;
    if (envelope->action == L"--apply") return ApplyWallpaper(envelope->requestId);
    if (envelope->action == L"--stop") {
        g.requestId = envelope->requestId;
        g.operation = "stop";
        StopPlayback(true);
        return WriteRuntimeStatus("stopped");
    }
    if (envelope->action == L"--pause") {
        SetUserPaused(true);
        return true;
    }
    if (envelope->action == L"--resume") {
        SetUserPaused(false);
        return true;
    }
    if (envelope->action == L"--toggle-mute") {
        g.settings.muted = !g.settings.muted;
        SaveSettings();
        ApplyAudio();
        RefreshTray();
        return true;
    }
    if (envelope->action == L"--exit") {
        g.shuttingDown = true;
        DestroyWindow(g.hostWindow);
        return true;
    }
    if (envelope->action == L"--open") {
        LaunchController();
        return true;
    }
    return false;
}

bool RelayToExisting(std::wstring_view command) {
    HANDLE ready = nullptr;
    for (int attempt = 0; attempt < 50 && !ready; ++attempt) {
        ready = OpenEventW(SYNCHRONIZE, FALSE, kReadyEvent);
        if (!ready) Sleep(100);
    }
    if (ready) {
        const DWORD wait = WaitForSingleObject(ready, 5000);
        CloseHandle(ready);
        if (wait != WAIT_OBJECT_0) return false;
    }
    HWND existing = FindWindowW(kHostClass, nullptr);
    if (!existing) return false;
    std::wstring copy(command);
    COPYDATASTRUCT data{};
    data.dwData = kCommandMagic;
    data.cbData = static_cast<DWORD>((copy.size() + 1) * sizeof(wchar_t));
    data.lpData = copy.data();
    DWORD_PTR result = 0;
    return SendMessageTimeoutW(existing, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
        SMTO_ABORTIFHUNG | SMTO_BLOCK, 5000, &result) != 0 && result != 0;
}

LRESULT CALLBACK WallpaperProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT bounds{};
        GetClientRect(window, &bounds);
        FillRect(dc, &bounds, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        EndPaint(window, &paint);
        if (g.mp4.IsOpen()) g.mp4.Paint();
        return 0;
    }
    case WM_SIZE:
        if (g.mp4.IsOpen()) g.mp4.Paint();
        return 0;
    case WM_NCDESTROY:
        if (window == g.wallpaperWindow) {
            g.wallpaperWindow = nullptr;
            ++g.mediaGeneration;
            KillTimer(g.hostWindow, kMp4Timer);
            PostMessageW(g.hostWindow, WM_APP_SURFACE_LOST, g.pendingApply ? 1 : 0, 0);
        }
        return DefWindowProcW(window, message, wParam, lParam);
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

LRESULT CALLBACK HostProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == g.taskbarCreated && g.taskbarCreated != 0) {
        UpdateTray(true);
        if (g.settings.playing) ApplyWallpaper();
        return 0;
    }
    switch (message) {
    case WM_CREATE:
        g.hostWindow = window;
        WTSRegisterSessionNotification(window, NOTIFY_FOR_THIS_SESSION);
        SetTimer(window, kShellTimer, 2000, nullptr);
        UpdateTray(true);
        return 0;
    case WM_COPYDATA: {
        const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
        if (!data || data->dwData != kCommandMagic || !data->lpData || data->cbData < sizeof(wchar_t) ||
            data->cbData > 256 * sizeof(wchar_t) || data->cbData % sizeof(wchar_t) != 0) return FALSE;
        const auto* text = static_cast<const wchar_t*>(data->lpData);
        const std::size_t count = data->cbData / sizeof(wchar_t);
        if (text[count - 1] != L'\0') return FALSE;
        return ProcessCommand(std::wstring_view(text, count - 1)) ? TRUE : FALSE;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kCmdOpen: LaunchController(); return 0;
        case kCmdPause: SetUserPaused(!g.userPaused); return 0;
        case kCmdMute: ProcessCommand(L"--toggle-mute"); return 0;
        case kCmdStop: ProcessCommand(L"--stop"); return 0;
        case kCmdExit: ProcessCommand(L"--exit"); return 0;
        default: break;
        }
        break;
    case WM_TIMER:
        if (wParam == kMp4Timer) {
            g.mp4.TickLoop();
        } else if (wParam == kApplyTimer) {
            KillTimer(window, kApplyTimer);
            ApplyWallpaper(g.requestId);
        } else if (wParam == kShellTimer && g.settings.playing &&
                   (!IsWindow(g.wallpaperWindow) || !g.shell.IsAttached(g.wallpaperWindow))) {
            ApplyWallpaper();
        }
        return 0;
    case WM_APP_MEDIA_READY: {
        if (static_cast<std::uint64_t>(lParam) != g.mediaGeneration) return 0;
        std::wstring error;
        const HRESULT readyResult = g.mp4.OnReady(g.settings.startMs, g.settings.endMs,
                                                  !IsEffectivelyPaused(), error);
        if (FAILED(readyResult)) {
            char errorCode[40]{};
            std::snprintf(errorCode, sizeof(errorCode), "media-ready-0x%08lX",
                          static_cast<unsigned long>(readyResult));
            StopPlayback(true);
            WriteRuntimeStatus("error", errorCode,
                error.empty() ? "The video opened but could not start playback." : NarrowAscii(error));
        } else {
            if (FAILED(ApplyAudio())) {
                StopPlayback(true);
                WriteRuntimeStatus("error", "media-audio-failed",
                    "Windows Media Foundation could not configure video audio.");
                return 0;
            }
            WriteRuntimeStatus("starting");
            RefreshTray();
        }
        return 0;
    }
    case WM_APP_MEDIA_ENDED:
        if (static_cast<std::uint64_t>(lParam) != g.mediaGeneration) return 0;
        g.mp4.OnPlaybackEnded();
        return 0;
    case WM_APP_MEDIA_POSITION_SET:
        if (static_cast<std::uint64_t>(lParam) != g.mediaGeneration) return 0;
        if (FAILED(g.mp4.OnPositionSet())) {
            StopPlayback(true);
            WriteRuntimeStatus("error", "media-play-failed",
                "Windows Media Foundation could not start video playback.");
        } else if (g.pendingApply) {
            g.pendingApply = false;
            WriteRuntimeStatus("playing");
            SetTimer(window, kMp4Timer, 30, nullptr);
            RefreshTray();
        }
        return 0;
    case WM_APP_MEDIA_ERROR:
        if (static_cast<std::uint64_t>(lParam) != g.mediaGeneration) return 0;
        StopPlayback(true);
        WriteRuntimeStatus("error", "media-playback-failed", "Video playback stopped because Windows reported a media error.");
        return 0;
    case WM_APP_SURFACE_LOST: {
        const bool commandWasPending = wParam != 0;
        ResetPlayers();
        g.pendingApply = g.settings.playing;
        if (!commandWasPending) {
            g.requestId.clear();
            g.operation = "recovery";
        }
        if (g.settings.playing && !g.shuttingDown) {
            SetTimer(window, kApplyTimer, 100, nullptr);
        }
        return 0;
    }
    case WM_APP_TRAY:
        if (LOWORD(lParam) == WM_LBUTTONDBLCLK) LaunchController();
        else if (LOWORD(lParam) == WM_CONTEXTMENU || LOWORD(lParam) == WM_RBUTTONUP) ShowTrayMenu();
        return 0;
    case WM_WTSSESSION_CHANGE:
        if (wParam == WTS_SESSION_LOCK) SetSessionLocked(true);
        else if (wParam == WTS_SESSION_UNLOCK) SetSessionLocked(false);
        return 0;
    case WM_POWERBROADCAST:
        if (wParam == PBT_APMSUSPEND) {
            g.powerSuspended = true;
            ApplyPauseState();
            return TRUE;
        }
        if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMECRITICAL ||
            wParam == PBT_APMRESUMESUSPEND) {
            g.powerSuspended = false;
            if (g.settings.playing &&
                (!IsWindow(g.wallpaperWindow) || !g.shell.IsAttached(g.wallpaperWindow))) {
                ApplyWallpaper();
            } else {
                ApplyPauseState();
            }
            return TRUE;
        }
        return TRUE;
    case WM_DISPLAYCHANGE:
        if (IsWindow(g.wallpaperWindow)) g.shell.SizeToVirtualDesktop(g.wallpaperWindow);
        if (g.mp4.IsOpen()) g.mp4.Paint();
        if (IsWindow(g.wallpaperWindow)) InvalidateRect(g.wallpaperWindow, nullptr, FALSE);
        return 0;
    case WM_DESTROY:
        KillTimer(window, kMp4Timer);
        KillTimer(window, kApplyTimer);
        KillTimer(window, kShellTimer);
        WTSUnRegisterSessionNotification(window);
        UpdateTray(false);
        g.mp4.Close();
        if (IsWindow(g.wallpaperWindow)) DestroyWindow(g.wallpaperWindow);
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool RegisterClasses() {
    WNDCLASSEXW wallpaper{};
    wallpaper.cbSize = sizeof(wallpaper);
    wallpaper.hInstance = g.instance;
    wallpaper.lpfnWndProc = WallpaperProc;
    wallpaper.lpszClassName = kWallpaperClass;
    wallpaper.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    if (!RegisterClassExW(&wallpaper) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    WNDCLASSEXW host{};
    host.cbSize = sizeof(host);
    host.hInstance = g.instance;
    host.lpfnWndProc = HostProc;
    host.lpszClassName = kHostClass;
    host.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    return RegisterClassExW(&host) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool QuerySessionLocked() {
    HDESK desktop = OpenInputDesktop(0, FALSE, DESKTOP_SWITCHDESKTOP);
    if (!desktop) return true;
    const bool locked = SwitchDesktop(desktop) == FALSE;
    CloseDesktop(desktop);
    return locked;
}

int SmokeTest() {
    const HRESULT mediaResult = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (SUCCEEDED(mediaResult)) MFShutdown();
    return SUCCEEDED(mediaResult) ? 0 : 2;
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(comResult)) return 1;
    std::wstring command = commandLine ? commandLine : L"";
    while (!command.empty() && iswspace(command.front())) command.erase(command.begin());
    while (!command.empty() && iswspace(command.back())) command.pop_back();
    if (command == L"--smoke") {
        const int result = SmokeTest();
        CoUninitialize();
        return result;
    }

    HANDLE mutex = CreateMutexW(nullptr, TRUE, kHostMutex);
    if (!mutex) {
        CoUninitialize();
        return 2;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        const bool relayed = RelayToExisting(command.empty() ? L"--apply" : command);
        CloseHandle(mutex);
        CoUninitialize();
        return relayed ? 0 : 3;
    }
    HANDLE readyEvent = CreateEventW(nullptr, TRUE, FALSE, kReadyEvent);
    if (!readyEvent) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        CoUninitialize();
        return 4;
    }
    const auto startupCommand = ParseCommandEnvelope(command.empty() ? L"--apply" : command);
    if (startupCommand && startupCommand->action == L"--exit") {
        CloseHandle(readyEvent);
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        CoUninitialize();
        return 0;
    }

    g.instance = instance;
    g.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    g.settingsPath = SettingsPath();
    g.statusPath = g.settingsPath.substr(0, g.settingsPath.find_last_of(L"\\/")) + L"\\runtime.status";
    LoadSettings();
    if (startupCommand && startupCommand->action == L"--stop") {
        g.requestId = startupCommand->requestId;
        g.operation = "stop";
        g.settings.playing = false;
        SaveSettings();
        const bool acknowledged = WriteRuntimeStatus("stopped");
        CloseHandle(readyEvent);
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        CoUninitialize();
        return acknowledged ? 0 : 7;
    }
    if (!RegisterClasses()) {
        CloseHandle(readyEvent);
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        CoUninitialize();
        return 5;
    }

    g.hostWindow = CreateWindowExW(WS_EX_TOOLWINDOW, kHostClass, L"LiveWallpaper Host",
        WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!g.hostWindow) {
        if (g.hostWindow) DestroyWindow(g.hostWindow);
        CloseHandle(readyEvent);
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        CoUninitialize();
        return 6;
    }
    g.sessionLocked = QuerySessionLocked();
    SetEvent(readyEvent);

    ProcessCommand(command.empty() ? L"--apply" : command);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    g.mp4.Shutdown();
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    CloseHandle(readyEvent);
    CoUninitialize();
    return static_cast<int>(message.wParam);
}
