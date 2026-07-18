#pragma once

#include <Windows.h>
#include <mfplay.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <string>

namespace livewallpaper {

class Mp4Player {
public:
    Mp4Player() = default;
    ~Mp4Player();
    Mp4Player(const Mp4Player&) = delete;
    Mp4Player& operator=(const Mp4Player&) = delete;

    HRESULT Initialize();
    HRESULT Open(HWND videoWindow, HWND notifyWindow, const std::wstring& path,
                 std::uint64_t generation);
    void Close();
    void Shutdown();
    HRESULT OnReady(std::int64_t requestedStartMs, std::int64_t requestedEndMs,
                    bool startPlayback, std::wstring& error);
    HRESULT OnPositionSet();
    void OnPlaybackEnded();
    void TickLoop();
    void Paint();
    HRESULT SetAudio(bool muted, int volume);
    HRESULT Pause();
    HRESULT Resume();

    bool IsOpen() const { return player_ != nullptr; }
    bool IsReady() const { return ready_; }
    std::int64_t DurationMs() const { return durationMs_; }

private:
    class Callback final : public IMFPMediaPlayerCallback {
    public:
        Callback(HWND notifyWindow, std::uint64_t generation)
            : notifyWindow_(notifyWindow), generation_(generation) {}
        STDMETHODIMP QueryInterface(REFIID iid, void** object) override;
        STDMETHODIMP_(ULONG) AddRef() override;
        STDMETHODIMP_(ULONG) Release() override;
        void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* eventHeader) override;
        void Detach() { notifyWindow_.store(nullptr); }
    private:
        std::atomic<ULONG> references_{1};
        std::atomic<HWND> notifyWindow_;
        const std::uint64_t generation_;
    };

    HRESULT SeekMs(std::int64_t milliseconds);
    HRESULT UpdateVideoLayout();
    static std::int64_t VariantToMs(const PROPVARIANT& value);

    Microsoft::WRL::ComPtr<IMFPMediaPlayer> player_;
    Microsoft::WRL::ComPtr<Callback> callback_;
    bool mfStarted_ = false;
    bool ready_ = false;
    bool suspended_ = false;
    bool seekInFlight_ = false;
    bool restartAfterSeek_ = false;
    HWND videoWindow_ = nullptr;
    SIZE videoAspectSize_{};
    SIZE lastDestinationSize_{};
    std::int64_t durationMs_ = 0;
    std::int64_t segmentStartMs_ = 0;
    std::int64_t segmentEndMs_ = 0;
};

} // namespace livewallpaper
