#include "../src/core.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {
int failures = 0;

void Check(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define CHECK(x) Check(!!(x), #x, __LINE__)

void SegmentTests() {
    using livewallpaper::ValidateSegment;
    CHECK(ValidateSegment(0, 0, 10'000).valid);
    CHECK(ValidateSegment(1000, 5000, 10'000).valid);
    CHECK(!ValidateSegment(-1, 5000, 10'000).valid);
    CHECK(!ValidateSegment(5000, 5000, 10'000).valid);
    CHECK(!ValidateSegment(6000, 5000, 10'000).valid);
    CHECK(!ValidateSegment(1000, 1249, 10'000).valid);
    CHECK(ValidateSegment(1000, 1250, 10'000).valid);
    CHECK(!ValidateSegment(0, 0, 249).valid);
    CHECK(!ValidateSegment(0, 10'001, 10'000).valid);
    CHECK(!ValidateSegment(0, 0, 0).valid);
    auto full = ValidateSegment(0, 0, 10'000);
    CHECK(full.startMs == 0 && full.endMs == 10'000);
}

void TimeParsingTests() {
    using livewallpaper::ParseMilliseconds;
    CHECK(ParseMilliseconds(L"").value_or(7) == 0);
    CHECK(ParseMilliseconds(L"1234").value_or(0) == 1234);
    CHECK(ParseMilliseconds(L" 42 ").value_or(0) == 42);
    CHECK(!ParseMilliseconds(L"-1"));
    CHECK(!ParseMilliseconds(L"1.5"));
    CHECK(!ParseMilliseconds(L"999999999999999999999"));
}

void SettingsTests() {
    livewallpaper::Settings input;
    input.mediaPath = L"C:\\media folder\\demo=wall.mp4";
    input.muted = false;
    input.volume = 73;
    input.startMs = 123;
    input.endMs = 4567;
    input.closeToTray = false;
    const std::string encoded = livewallpaper::SerializeSettings(input);
    auto decoded = livewallpaper::DeserializeSettings(encoded);
    CHECK(decoded.has_value());
    CHECK(decoded->mediaPath == input.mediaPath);
    CHECK(decoded->muted == input.muted);
    CHECK(decoded->volume == input.volume);
    CHECK(decoded->startMs == input.startMs);
    CHECK(decoded->endMs == input.endMs);
    CHECK(decoded->closeToTray == input.closeToTray);

    auto defaults = livewallpaper::DeserializeSettings("version=1\nvolume=999\nstart_ms=nope\n");
    CHECK(defaults.has_value());
    CHECK(defaults->volume == 100);
    CHECK(defaults->startMs == 0);
}

void VideoFormatTests() {
    CHECK(livewallpaper::IsSupportedVideoExtension(L".mp4"));
    CHECK(livewallpaper::IsSupportedVideoExtension(L".WEBM"));
    CHECK(livewallpaper::IsSupportedVideoExtension(L".mkv"));
    CHECK(livewallpaper::IsSupportedVideoExtension(L".Mov"));
    CHECK(!livewallpaper::IsSupportedVideoExtension(L".jpg"));
    CHECK(!livewallpaper::IsSupportedVideoExtension(L"mp4"));
    CHECK(!livewallpaper::IsSupportedVideoExtension(L""));
}

void CommandEnvelopeTests() {
    const auto correlated = livewallpaper::ParseCommandEnvelope(L"--apply|abc-123");
    CHECK(correlated.has_value());
    CHECK(correlated->action == L"--apply");
    CHECK(correlated->requestId == L"abc-123");

    const auto legacy = livewallpaper::ParseCommandEnvelope(L"--stop");
    CHECK(legacy.has_value());
    CHECK(legacy->action == L"--stop");
    CHECK(legacy->requestId.empty());

    CHECK(!livewallpaper::ParseCommandEnvelope(L"--apply|"));
    CHECK(!livewallpaper::ParseCommandEnvelope(L"--apply|bad id"));
    CHECK(!livewallpaper::ParseCommandEnvelope(L"--apply|one|two"));
    CHECK(!livewallpaper::ParseCommandEnvelope(L"|request"));
}

void RuntimeStatusTests() {
    livewallpaper::RuntimeStatus status;
    status.requestId = "request-42";
    status.operation = "apply";
    status.state = "error";
    status.errorCode = "codec-unavailable";
    status.message = "Codec unavailable\nInstall a Media Foundation codec.";
    const std::string encoded = livewallpaper::SerializeRuntimeStatus(status);
    const auto decoded = livewallpaper::DeserializeRuntimeStatus(encoded);
    CHECK(decoded.has_value());
    CHECK(decoded->requestId == status.requestId);
    CHECK(decoded->operation == status.operation);
    CHECK(decoded->state == status.state);
    CHECK(decoded->errorCode == status.errorCode);
    CHECK(decoded->message == status.message);
    CHECK(!livewallpaper::DeserializeRuntimeStatus("version=2\nstate=playing\n"));
}

void ScalingTests() {
    using livewallpaper::ComputeCoverCrop;
    const auto fourKTo1080 = ComputeCoverCrop(3840, 2160, 1920, 1080);
    CHECK(std::abs(fourKTo1080.left) < 0.000001);
    CHECK(std::abs(fourKTo1080.top) < 0.000001);
    CHECK(std::abs(fourKTo1080.right - 1.0) < 0.000001);
    CHECK(std::abs(fourKTo1080.bottom - 1.0) < 0.000001);

    const auto fullHdTo1440 = ComputeCoverCrop(1920, 1080, 2560, 1440);
    CHECK(std::abs(fullHdTo1440.left) < 0.000001);
    CHECK(std::abs(fullHdTo1440.bottom - 1.0) < 0.000001);

    const auto wideDisplay = ComputeCoverCrop(3840, 2160, 3440, 1440);
    CHECK(std::abs(wideDisplay.left) < 0.000001);
    CHECK(wideDisplay.top > 0.12 && wideDisplay.top < 0.13);
    CHECK(wideDisplay.bottom > 0.87 && wideDisplay.bottom < 0.88);

    const auto portraitDisplay = ComputeCoverCrop(1920, 1080, 1080, 1920);
    CHECK(portraitDisplay.left > 0.34 && portraitDisplay.left < 0.35);
    CHECK(portraitDisplay.right > 0.65 && portraitDisplay.right < 0.66);
    CHECK(std::abs(portraitDisplay.top) < 0.000001);

    const auto invalid = ComputeCoverCrop(0, 1080, 1920, 1080);
    CHECK(invalid.left == 0 && invalid.top == 0 && invalid.right == 1 && invalid.bottom == 1);
}
}

int main() {
    SegmentTests();
    TimeParsingTests();
    SettingsTests();
    VideoFormatTests();
    CommandEnvelopeTests();
    RuntimeStatusTests();
    ScalingTests();
    if (failures) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All core tests passed\n";
    return 0;
}
