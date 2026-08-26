#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace altitude_audio_guard {

constexpr int kCheckIntervalSeconds = 60;

struct Config {
    bool enabled = false;
    std::string inputDeviceId;
    std::string outputDeviceId;
    std::string inputDeviceMatch;
    std::string outputDeviceMatch;
};

enum class Status {
    Disabled,
    NoDesiredDevices,
    Unchanged,
    Updated,
    Failed,
};

struct Result {
    Status status = Status::Disabled;
    std::string detail;
};

using Log = std::function<void(const std::string&)>;

std::string normalizeDeviceId(bool input, const std::string& rawId);

std::string updateConfigText(
    const std::string& current,
    const std::string& desiredInput,
    const std::string& desiredOutput,
    bool& changed
);

Result ensureConfig(
    const std::filesystem::path& path,
    const Config& config,
    bool verbose,
    const Log& log
);

} // namespace altitude_audio_guard
