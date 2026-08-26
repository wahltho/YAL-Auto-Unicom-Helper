#pragma once

#include "auto_unicom_voice.h"

#include <filesystem>
#include <string>
#include <vector>

struct HelperConfig {
    std::string autoUnicomMode = "off";
    std::string altitudeCallsign;
    int autoUnicomFrequencyKhz = 122800;
    std::string altitudeWindowTitle = "IVAO Pilot Client: Altitude";
    std::string autoUnicomMessageFieldName = "Message";
    std::string autoUnicomSendButtonText = "SEND";
    int autoUnicomConfirmTimeoutMs = 5000;
    int autoUnicomGateMaxAgeMs = 2500;
    int autoUnicomFinalGateTimeoutMs = 2500;
    int uiaRetryMs = 100;
    bool debugUia = false;

    bool autoUnicomChime = true;
    std::string autoUnicomChimeFile = "resources/auto_unicom_chime.wav";

    auto_unicom_voice::DeliveryMode autoUnicomVoiceMode =
        auto_unicom_voice::DeliveryMode::Off;
    std::string autoUnicomVoiceOutput;
    std::string autoUnicomVoiceOutputMatch;
    std::string autoUnicomVoiceLocalOutput;
    std::string autoUnicomVoiceLocalOutputMatch;
    std::string autoUnicomVoiceSapiVoice;
    int autoUnicomVoiceSapiRate = 0;
    int autoUnicomVoiceVolume = 100;
    int autoUnicomVoicePttLeadMs = 250;
    int autoUnicomVoicePttTailMs = 250;
    int autoUnicomVoiceReceiveWaitMs = 30000;
    int autoUnicomVoiceReceiveQuietMs = 1000;
    int autoUnicomVoicePttConfirmMs = 2500;
    std::string autoUnicomVoiceTestText = "Auto Unicom voice audio test";
    std::string pttKey = "LCTRL";
};

struct ConfigLoadResult {
    bool ok = false;
    bool fileMissing = false;
    std::string error;
    std::vector<std::string> unknownKeys;
};

ConfigLoadResult loadHelperConfigFile(
    const std::filesystem::path& path,
    HelperConfig& outConfig
);

bool saveHelperConfigFile(
    const std::filesystem::path& path,
    const HelperConfig& config,
    std::string& error
);

std::vector<std::string> diffHelperConfig(
    const HelperConfig& left,
    const HelperConfig& right
);

std::string normalizeKeyName(const std::string& value);
bool isSupportedPttKey(const std::string& value);

struct AutoUnicomControlModes {
    std::string textMode;
    auto_unicom_voice::DeliveryMode voiceMode = auto_unicom_voice::DeliveryMode::Off;
};

AutoUnicomControlModes getAutoUnicomControlModes(const HelperConfig& config);
bool isAutoUnicomTextEnabled(const AutoUnicomControlModes& modes);
void setAutoUnicomTextEnabled(AutoUnicomControlModes& modes, bool enabled);
void setAutoUnicomVoiceMode(
    AutoUnicomControlModes& modes,
    auto_unicom_voice::DeliveryMode mode
);
