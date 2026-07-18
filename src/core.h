#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace livewallpaper {

struct Segment {
    bool valid = false;
    std::int64_t startMs = 0;
    std::int64_t endMs = 0;
    std::wstring error;
};

struct NormalizedCrop {
    double left = 0;
    double top = 0;
    double right = 1;
    double bottom = 1;
};

struct CommandEnvelope {
    std::wstring action;
    std::wstring requestId;
};

struct RuntimeStatus {
    std::string requestId;
    std::string operation;
    std::string state;
    std::string errorCode;
    std::string message;
};

NormalizedCrop ComputeCoverCrop(std::uint32_t sourceWidth, std::uint32_t sourceHeight,
                                std::uint32_t destinationWidth, std::uint32_t destinationHeight);

Segment ValidateSegment(std::int64_t startMs, std::int64_t endMs, std::int64_t durationMs);
std::optional<std::int64_t> ParseMilliseconds(const std::wstring& text);
bool IsSupportedVideoExtension(std::wstring_view extension);
std::optional<CommandEnvelope> ParseCommandEnvelope(std::wstring_view command);
std::string SerializeRuntimeStatus(const RuntimeStatus& status);
std::optional<RuntimeStatus> DeserializeRuntimeStatus(const std::string& text);

struct Settings {
    std::wstring mediaPath;
    bool muted = true;
    int volume = 50;
    std::int64_t startMs = 0;
    std::int64_t endMs = 0; // Zero means the natural media duration.
    bool closeToTray = true;
    bool playing = true;
};

std::string SerializeSettings(const Settings& settings);
std::optional<Settings> DeserializeSettings(const std::string& text);

} // namespace livewallpaper
