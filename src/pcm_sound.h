#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct PcmSound {
    std::vector<std::uint8_t> pcm;
    int frequency = 0;
    int channels = 0;
    std::filesystem::path loadedPath;
};

using PcmLog = void (*)(const std::string&);

bool ensurePcmSoundLoaded(
    const std::filesystem::path& pluginRoot,
    const std::string& configuredPath,
    PcmSound& sound,
    PcmLog log
);

bool playPcmSound(PcmSound& sound, void* refcon = nullptr);
void resetPcmSound(PcmSound& sound);
