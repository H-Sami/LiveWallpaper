#include "core.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <charconv>
#include <limits>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace livewallpaper {
namespace {

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        result.data(), count, nullptr, nullptr);
    return result;
}

std::optional<std::wstring> Utf8ToWide(const std::string& value) {
    if (value.empty()) return std::wstring{};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return std::nullopt;
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), count)) return std::nullopt;
    return result;
}

std::string Escape(const std::string& value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size());
    for (const unsigned char c : value) {
        if (c == '%' || c == '\n' || c == '\r' || c == '=') {
            out.push_back('%'); out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]);
        } else out.push_back(static_cast<char>(c));
    }
    return out;
}

int Hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

std::optional<std::string> Unescape(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '%') { out.push_back(value[i]); continue; }
        if (i + 2 >= value.size()) return std::nullopt;
        const int high = Hex(value[i + 1]), low = Hex(value[i + 2]);
        if (high < 0 || low < 0) return std::nullopt;
        out.push_back(static_cast<char>((high << 4) | low));
        i += 2;
    }
    return out;
}

template <typename T>
std::optional<T> ParseInteger(std::string_view value) {
    T result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) return std::nullopt;
    return result;
}

bool ParseBool(const std::unordered_map<std::string, std::string>& values,
               const char* key, bool fallback) {
    const auto it = values.find(key);
    if (it == values.end()) return fallback;
    if (it->second == "1" || it->second == "true") return true;
    if (it->second == "0" || it->second == "false") return false;
    return fallback;
}
}

NormalizedCrop ComputeCoverCrop(std::uint32_t sourceWidth, std::uint32_t sourceHeight,
                                std::uint32_t destinationWidth, std::uint32_t destinationHeight) {
    NormalizedCrop crop;
    if (sourceWidth == 0 || sourceHeight == 0 || destinationWidth == 0 || destinationHeight == 0)
        return crop;

    const std::uint64_t sourceScaledWidth =
        static_cast<std::uint64_t>(sourceWidth) * destinationHeight;
    const std::uint64_t sourceScaledHeight =
        static_cast<std::uint64_t>(sourceHeight) * destinationWidth;
    if (sourceScaledWidth > sourceScaledHeight) {
        const double visibleWidth = static_cast<double>(sourceScaledHeight) /
                                    static_cast<double>(sourceScaledWidth);
        crop.left = (1.0 - visibleWidth) / 2.0;
        crop.right = crop.left + visibleWidth;
    } else if (sourceScaledHeight > sourceScaledWidth) {
        const double visibleHeight = static_cast<double>(sourceScaledWidth) /
                                     static_cast<double>(sourceScaledHeight);
        crop.top = (1.0 - visibleHeight) / 2.0;
        crop.bottom = crop.top + visibleHeight;
    }
    return crop;
}

Segment ValidateSegment(std::int64_t startMs, std::int64_t endMs, std::int64_t durationMs) {
    Segment result;
    if (durationMs <= 0) { result.error = L"The media duration is unavailable."; return result; }
    if (startMs < 0 || endMs < 0) { result.error = L"Start and end must be non-negative milliseconds."; return result; }
    const auto resolvedEnd = endMs == 0 ? durationMs : endMs;
    if (startMs >= resolvedEnd) { result.error = L"Start time must be earlier than end time."; return result; }
    if (resolvedEnd > durationMs) { result.error = L"End time exceeds the media duration."; return result; }
    if (resolvedEnd - startMs < 250) {
        result.error = L"The loop range must be at least 250 milliseconds.";
        return result;
    }
    result.valid = true;
    result.startMs = startMs;
    result.endMs = resolvedEnd;
    return result;
}

std::optional<std::int64_t> ParseMilliseconds(const std::wstring& text) {
    const auto first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return 0;
    const auto last = text.find_last_not_of(L" \t\r\n");
    const auto trimmed = text.substr(first, last - first + 1);
    std::int64_t result = 0;
    for (wchar_t c : trimmed) {
        if (c < L'0' || c > L'9') return std::nullopt;
        const int digit = c - L'0';
        if (result > (std::numeric_limits<std::int64_t>::max() - digit) / 10) return std::nullopt;
        result = result * 10 + digit;
    }
    return result;
}

std::string SerializeSettings(const Settings& s) {
    std::ostringstream out;
    out << "version=1\n"
        << "media=" << Escape(WideToUtf8(s.mediaPath)) << '\n'
        << "muted=" << (s.muted ? 1 : 0) << '\n'
        << "volume=" << std::clamp(s.volume, 0, 100) << '\n'
        << "start_ms=" << std::max<std::int64_t>(0, s.startMs) << '\n'
        << "end_ms=" << std::max<std::int64_t>(0, s.endMs) << '\n'
        << "close_to_tray=" << (s.closeToTray ? 1 : 0) << '\n'
        << "playing=" << (s.playing ? 1 : 0) << '\n';
    return out.str();
}

std::optional<Settings> DeserializeSettings(const std::string& text) {
    std::unordered_map<std::string, std::string> values;
    std::size_t offset = 0;
    while (offset <= text.size()) {
        const auto end = text.find('\n', offset);
        std::string_view line(text.data() + offset,
            (end == std::string::npos ? text.size() : end) - offset);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (!line.empty()) {
            const auto equals = line.find('=');
            if (equals != std::string_view::npos) {
                auto decoded = Unescape(line.substr(equals + 1));
                if (decoded) values[std::string(line.substr(0, equals))] = std::move(*decoded);
            }
        }
        if (end == std::string::npos) break;
        offset = end + 1;
    }
    if (const auto it = values.find("version"); it != values.end() && it->second != "1") return std::nullopt;
    Settings result;
    if (const auto it = values.find("media"); it != values.end()) {
        auto wide = Utf8ToWide(it->second);
        if (!wide) return std::nullopt;
        result.mediaPath = std::move(*wide);
    }
    result.muted = ParseBool(values, "muted", result.muted);
    result.closeToTray = ParseBool(values, "close_to_tray", result.closeToTray);
    result.playing = ParseBool(values, "playing", result.playing);
    if (const auto it = values.find("volume"); it != values.end()) {
        if (auto value = ParseInteger<int>(it->second)) result.volume = std::clamp(*value, 0, 100);
    }
    if (const auto it = values.find("start_ms"); it != values.end()) {
        if (auto value = ParseInteger<std::int64_t>(it->second); value && *value >= 0) result.startMs = *value;
    }
    if (const auto it = values.find("end_ms"); it != values.end()) {
        if (auto value = ParseInteger<std::int64_t>(it->second); value && *value >= 0) result.endMs = *value;
    }
    return result;
}

bool IsSupportedVideoExtension(std::wstring_view extension) {
    static constexpr std::wstring_view supported[] = {L".mp4", L".webm", L".mkv", L".mov"};
    return std::ranges::any_of(supported, [extension](std::wstring_view candidate) {
        return extension.size() == candidate.size() &&
            CompareStringOrdinal(extension.data(), static_cast<int>(extension.size()),
                candidate.data(), static_cast<int>(candidate.size()), TRUE) == CSTR_EQUAL;
    });
}

std::optional<CommandEnvelope> ParseCommandEnvelope(std::wstring_view command) {
    if (command.empty()) return std::nullopt;
    const std::size_t separator = command.find(L'|');
    CommandEnvelope envelope;
    if (separator == std::wstring_view::npos) {
        envelope.action.assign(command);
        return envelope;
    }
    if (separator == 0 || separator + 1 >= command.size() ||
        command.find(L'|', separator + 1) != std::wstring_view::npos) return std::nullopt;
    envelope.action.assign(command.substr(0, separator));
    envelope.requestId.assign(command.substr(separator + 1));
    if (envelope.requestId.size() > 64 ||
        !std::ranges::all_of(envelope.requestId, [](wchar_t c) {
            return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
                   (c >= L'0' && c <= L'9') || c == L'-';
        })) return std::nullopt;
    return envelope;
}

std::string SerializeRuntimeStatus(const RuntimeStatus& status) {
    std::ostringstream out;
    out << "version=1\n"
        << "request_id=" << Escape(status.requestId) << '\n'
        << "operation=" << Escape(status.operation) << '\n'
        << "state=" << Escape(status.state) << '\n'
        << "error_code=" << Escape(status.errorCode) << '\n'
        << "message=" << Escape(status.message) << '\n';
    return out.str();
}

std::optional<RuntimeStatus> DeserializeRuntimeStatus(const std::string& text) {
    std::unordered_map<std::string, std::string> values;
    std::size_t offset = 0;
    while (offset <= text.size()) {
        const auto end = text.find('\n', offset);
        std::string_view line(text.data() + offset,
            (end == std::string::npos ? text.size() : end) - offset);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const auto equals = line.find('=');
        if (equals != std::string_view::npos) {
            auto decoded = Unescape(line.substr(equals + 1));
            if (!decoded) return std::nullopt;
            values[std::string(line.substr(0, equals))] = std::move(*decoded);
        }
        if (end == std::string::npos) break;
        offset = end + 1;
    }
    if (values["version"] != "1") return std::nullopt;
    RuntimeStatus status;
    status.requestId = values["request_id"];
    status.operation = values["operation"];
    status.state = values["state"];
    status.errorCode = values["error_code"];
    status.message = values["message"];
    return status;
}

} // namespace livewallpaper
