#include "pcm_sound.h"

#include "XPLMSound.h"

#include <cstring>
#include <fstream>
#include <iterator>

namespace {

bool loadWavPcm16(
    const std::filesystem::path& path,
    std::vector<std::uint8_t>& outPcm,
    int& outFrequency,
    int& outChannels
) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }
    std::vector<std::uint8_t> data(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
    if (data.size() < 44) {
        return false;
    }
    const auto read32 = [&](std::size_t offset) -> std::uint32_t {
        return static_cast<std::uint32_t>(data[offset]) |
            (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
            (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
            (static_cast<std::uint32_t>(data[offset + 3]) << 24);
    };
    const auto read16 = [&](std::size_t offset) -> std::uint16_t {
        return static_cast<std::uint16_t>(data[offset]) |
            (static_cast<std::uint16_t>(data[offset + 1]) << 8);
    };
    if (std::memcmp(data.data(), "RIFF", 4) != 0 ||
        std::memcmp(data.data() + 8, "WAVE", 4) != 0) {
        return false;
    }

    bool formatFound = false;
    bool samplesFound = false;
    std::uint16_t audioFormat = 0;
    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t bitsPerSample = 0;
    std::size_t position = 12;
    while (position + 8 <= data.size()) {
        const char* id = reinterpret_cast<const char*>(data.data() + position);
        const std::uint32_t size = read32(position + 4);
        const std::size_t chunkStart = position + 8;
        if (chunkStart + size > data.size()) {
            break;
        }
        if (std::memcmp(id, "fmt ", 4) == 0 && size >= 16) {
            audioFormat = read16(chunkStart);
            channels = read16(chunkStart + 2);
            sampleRate = read32(chunkStart + 4);
            bitsPerSample = read16(chunkStart + 14);
            formatFound = true;
        } else if (std::memcmp(id, "data", 4) == 0) {
            outPcm.assign(
                data.begin() + static_cast<std::ptrdiff_t>(chunkStart),
                data.begin() + static_cast<std::ptrdiff_t>(chunkStart + size));
            samplesFound = true;
        }
        position = chunkStart + size + (size % 2);
    }

    if (!formatFound || !samplesFound || audioFormat != 1 || bitsPerSample != 16 ||
        channels < 1 || channels > 2 || sampleRate == 0 || outPcm.empty()) {
        return false;
    }
    outFrequency = static_cast<int>(sampleRate);
    outChannels = static_cast<int>(channels);
    return true;
}

} // namespace

bool ensurePcmSoundLoaded(
    const std::filesystem::path& pluginRoot,
    const std::string& configuredPath,
    PcmSound& sound,
    PcmLog log
) {
    std::filesystem::path path = configuredPath;
    if (path.is_relative()) {
        path = pluginRoot / path;
    }
    if (!sound.pcm.empty() && sound.loadedPath == path) {
        return true;
    }

    resetPcmSound(sound);
    if (!loadWavPcm16(path, sound.pcm, sound.frequency, sound.channels)) {
        if (log) {
            log("Auto UNICOM chime: failed to load " + path.string());
        }
        return false;
    }
    sound.loadedPath = path;
    if (log) {
        log("Auto UNICOM chime: loaded " + path.string());
    }
    return true;
}

bool playPcmSound(PcmSound& sound, void* refcon) {
    if (sound.pcm.empty() || sound.frequency <= 0 || sound.channels <= 0) {
        return false;
    }
    return XPLMPlayPCMOnBus(
        sound.pcm.data(),
        static_cast<std::uint32_t>(sound.pcm.size()),
        FMOD_SOUND_FORMAT_PCM16,
        sound.frequency,
        sound.channels,
        0,
        xplm_AudioUI,
        nullptr,
        refcon) != nullptr;
}

void resetPcmSound(PcmSound& sound) {
    sound = PcmSound{};
}
