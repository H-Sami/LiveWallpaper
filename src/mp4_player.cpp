#include "mp4_player.h"

#include "app_messages.h"
#include "core.h"

#include <mfapi.h>
#include <mferror.h>
#include <propvarutil.h>

namespace livewallpaper {

Mp4Player::~Mp4Player() {
    Shutdown();
}

HRESULT Mp4Player::Initialize() {
    if (mfStarted_) return S_OK;
    const HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (SUCCEEDED(hr)) mfStarted_ = true;
    return hr;
}

HRESULT Mp4Player::Callback::QueryInterface(REFIID iid, void** object) {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (iid == __uuidof(IUnknown) || iid == __uuidof(IMFPMediaPlayerCallback)) {
        *object = static_cast<IMFPMediaPlayerCallback*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG Mp4Player::Callback::AddRef() { return ++references_; }
ULONG Mp4Player::Callback::Release() {
    const ULONG value = --references_;
    if (!value) delete this;
    return value;
}

void Mp4Player::Callback::OnMediaPlayerEvent(MFP_EVENT_HEADER* header) {
    if (!header) return;
    const HWND target = notifyWindow_.load();
    if (!target) return;
    if (FAILED(header->hrEvent)) {
        PostMessageW(target, WM_APP_MEDIA_ERROR, static_cast<WPARAM>(header->hrEvent),
                     static_cast<LPARAM>(generation_));
        return;
    }
    switch (header->eEventType) {
    case MFP_EVENT_TYPE_MEDIAITEM_SET:
        PostMessageW(target, WM_APP_MEDIA_READY, 0, static_cast<LPARAM>(generation_));
        break;
    case MFP_EVENT_TYPE_PLAYBACK_ENDED:
        PostMessageW(target, WM_APP_MEDIA_ENDED, 0, static_cast<LPARAM>(generation_));
        break;
    case MFP_EVENT_TYPE_POSITION_SET:
        PostMessageW(target, WM_APP_MEDIA_POSITION_SET, 0, static_cast<LPARAM>(generation_));
        break;
    case MFP_EVENT_TYPE_ERROR:
        PostMessageW(target, WM_APP_MEDIA_ERROR, static_cast<WPARAM>(header->hrEvent),
                     static_cast<LPARAM>(generation_));
        break;
    default:
        break;
    }
}

HRESULT Mp4Player::Open(HWND videoWindow, HWND notifyWindow, const std::wstring& path,
                        std::uint64_t generation) {
    Close();
    videoWindow_ = videoWindow;
    MSG stale{};
    while (PeekMessageW(&stale, notifyWindow, WM_APP_MEDIA_READY, WM_APP_MEDIA_ERROR,
                        PM_REMOVE)) {}
    HRESULT hr = Initialize();
    if (FAILED(hr)) return hr;
    callback_.Attach(new (std::nothrow) Callback(notifyWindow, generation));
    if (!callback_) return E_OUTOFMEMORY;
    hr = MFPCreateMediaPlayer(path.c_str(), FALSE, MFP_OPTION_NONE, callback_.Get(),
        videoWindow, player_.GetAddressOf());
    if (FAILED(hr)) {
        callback_->Detach();
        callback_.Reset();
        return hr;
    }
    player_->SetBorderColor(RGB(0, 0, 0));
    return S_OK;
}

void Mp4Player::Close() {
    ready_ = false;
    suspended_ = false;
    seekInFlight_ = false;
    restartAfterSeek_ = false;
    durationMs_ = segmentStartMs_ = segmentEndMs_ = 0;
    videoWindow_ = nullptr;
    videoAspectSize_ = {};
    lastDestinationSize_ = {};
    if (callback_) callback_->Detach();
    if (player_) player_->Shutdown();
    player_.Reset();
    callback_.Reset();
}

void Mp4Player::Shutdown() {
    Close();
    if (mfStarted_) {
        MFShutdown();
        mfStarted_ = false;
    }
}

std::int64_t Mp4Player::VariantToMs(const PROPVARIANT& value) {
    if (value.vt == VT_I8) return value.hVal.QuadPart / 10'000;
    if (value.vt == VT_UI8) return static_cast<std::int64_t>(value.uhVal.QuadPart / 10'000);
    return 0;
}

HRESULT Mp4Player::SeekMs(std::int64_t milliseconds) {
    if (!player_) return MF_E_NOT_INITIALIZED;
    PROPVARIANT position;
    PropVariantInit(&position);
    position.vt = VT_I8;
    position.hVal.QuadPart = milliseconds * 10'000;
    return player_->SetPosition(MFP_POSITIONTYPE_100NS, &position);
}

HRESULT Mp4Player::OnReady(std::int64_t requestedStartMs, std::int64_t requestedEndMs,
                           bool startPlayback, std::wstring& error) {
    if (!player_) return MF_E_NOT_INITIALIZED;
    PROPVARIANT duration;
    PropVariantInit(&duration);
    HRESULT hr = player_->GetDuration(MFP_POSITIONTYPE_100NS, &duration);
    if (FAILED(hr)) { error = L"Windows Media Foundation could not read the video duration."; return hr; }
    durationMs_ = VariantToMs(duration);
    PropVariantClear(&duration);
    const Segment segment = ValidateSegment(requestedStartMs, requestedEndMs, durationMs_);
    if (!segment.valid) { error = segment.error; return E_INVALIDARG; }
    segmentStartMs_ = segment.startMs;
    segmentEndMs_ = segment.endMs;
    SIZE nativeSize{};
    SIZE aspectSize{};
    hr = player_->GetNativeVideoSize(&nativeSize, &aspectSize);
    if (FAILED(hr) || nativeSize.cx <= 0 || nativeSize.cy <= 0) {
        error = L"Windows Media Foundation did not find a usable video stream.";
        return FAILED(hr) ? hr : MF_E_INVALIDMEDIATYPE;
    }
    videoAspectSize_ = aspectSize.cx > 0 && aspectSize.cy > 0 ? aspectSize : nativeSize;
    player_->SetAspectRatioMode(MFVideoARMode_PreservePicture);
    lastDestinationSize_ = {};
    hr = UpdateVideoLayout();
    if (FAILED(hr)) {
        error = L"Windows Media Foundation could not configure wallpaper scaling.";
        return hr;
    }
    ready_ = true;
    suspended_ = !startPlayback;
    seekInFlight_ = true;
    restartAfterSeek_ = startPlayback;
    hr = SeekMs(segmentStartMs_);
    if (FAILED(hr)) {
        ready_ = false;
        seekInFlight_ = false;
        restartAfterSeek_ = false;
    }
    return hr;
}

HRESULT Mp4Player::OnPositionSet() {
    if (!ready_ || !player_) return MF_E_NOT_INITIALIZED;
    seekInFlight_ = false;
    const bool restart = restartAfterSeek_ && !suspended_;
    restartAfterSeek_ = false;
    return restart ? player_->Play() : S_OK;
}

void Mp4Player::OnPlaybackEnded() {
    if (!ready_ || suspended_ || seekInFlight_) return;
    seekInFlight_ = true;
    restartAfterSeek_ = true;
    if (FAILED(SeekMs(segmentStartMs_))) {
        seekInFlight_ = false;
        restartAfterSeek_ = false;
    }
}

void Mp4Player::TickLoop() {
    if (!ready_ || suspended_ || seekInFlight_ || !player_) return;
    PROPVARIANT position;
    PropVariantInit(&position);
    if (SUCCEEDED(player_->GetPosition(MFP_POSITIONTYPE_100NS, &position))) {
        if (VariantToMs(position) >= segmentEndMs_ - 20) {
            seekInFlight_ = true;
            restartAfterSeek_ = true;
            if (FAILED(SeekMs(segmentStartMs_))) {
                seekInFlight_ = false;
                restartAfterSeek_ = false;
            }
        }
    }
    PropVariantClear(&position);
}

HRESULT Mp4Player::UpdateVideoLayout() {
    if (!player_ || !IsWindow(videoWindow_) || videoAspectSize_.cx <= 0 || videoAspectSize_.cy <= 0)
        return MF_E_NOT_INITIALIZED;
    RECT client{};
    if (!GetClientRect(videoWindow_, &client)) return HRESULT_FROM_WIN32(GetLastError());
    const LONG width = client.right - client.left;
    const LONG height = client.bottom - client.top;
    if (width <= 0 || height <= 0) return S_FALSE;
    if (lastDestinationSize_.cx == width && lastDestinationSize_.cy == height) return S_OK;

    const NormalizedCrop crop = ComputeCoverCrop(
        static_cast<std::uint32_t>(videoAspectSize_.cx), static_cast<std::uint32_t>(videoAspectSize_.cy),
        static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
    const MFVideoNormalizedRect source{
        static_cast<float>(crop.left), static_cast<float>(crop.top),
        static_cast<float>(crop.right), static_cast<float>(crop.bottom)};
    const HRESULT hr = player_->SetVideoSourceRect(&source);
    if (SUCCEEDED(hr)) lastDestinationSize_ = {width, height};
    return hr;
}

void Mp4Player::Paint() {
    if (!player_) return;
    UpdateVideoLayout();
    player_->UpdateVideo();
}

HRESULT Mp4Player::SetAudio(bool muted, int volume) {
    if (!player_) return MF_E_NOT_INITIALIZED;
    HRESULT hr = player_->SetMute(muted ? TRUE : FALSE);
    if (FAILED(hr)) return hr;
    return player_->SetVolume(
        static_cast<float>((volume < 0 ? 0 : volume > 100 ? 100 : volume)) / 100.0F);
}

HRESULT Mp4Player::Pause() {
    if (!player_ || !ready_) return MF_E_NOT_INITIALIZED;
    if (suspended_) return S_OK;
    const HRESULT hr = player_->Pause();
    if (SUCCEEDED(hr)) suspended_ = true;
    return hr;
}

HRESULT Mp4Player::Resume() {
    if (!player_ || !ready_) return MF_E_NOT_INITIALIZED;
    if (!suspended_) return S_OK;
    if (seekInFlight_) {
        restartAfterSeek_ = true;
        suspended_ = false;
        return S_OK;
    }
    const HRESULT hr = player_->Play();
    if (SUCCEEDED(hr)) suspended_ = false;
    return hr;
}

} // namespace livewallpaper
