#include "helper_config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace {

constexpr int kFrequencyMinKhz = 100000;
constexpr int kFrequencyMaxKhz = 200000;
constexpr int kConfirmTimeoutMinMs = 1000;
constexpr int kConfirmTimeoutMaxMs = 30000;
constexpr int kGateMaxAgeMinMs = 1000;
constexpr int kGateMaxAgeMaxMs = 5000;
constexpr int kFinalGateTimeoutMinMs = 1000;
constexpr int kFinalGateTimeoutMaxMs = 5000;
constexpr int kUiaRetryMinMs = 50;
constexpr int kUiaRetryMaxMs = 1000;
constexpr int kVoiceRateMin = -10;
constexpr int kVoiceRateMax = 10;
constexpr int kVoiceLeadMaxMs = 2000;
constexpr int kVoiceTailMaxMs = 3000;
constexpr int kVoiceReceiveWaitMaxMs = 30000;
constexpr int kVoiceReceiveQuietMaxMs = 5000;
constexpr int kVoiceConfirmMinMs = 500;
constexpr int kVoiceConfirmMaxMs = 5000;

std::string trim(std::string value) {
    const auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::string upperAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

int parseInt(const std::string& value) {
    std::size_t used = 0;
    int parsed = std::stoi(trim(value), &used);
    if (used != trim(value).size()) {
        throw std::invalid_argument("trailing characters");
    }
    return parsed;
}

bool parseBool(const std::string& value) {
    const std::string normalized = upperAscii(trim(value));
    if (normalized == "1" || normalized == "TRUE" || normalized == "YES" || normalized == "ON") {
        return true;
    }
    if (normalized == "0" || normalized == "FALSE" || normalized == "NO" || normalized == "OFF") {
        return false;
    }
    throw std::invalid_argument("expected boolean");
}

template <typename T>
T clampValue(T value, T minimum, T maximum) {
    return std::max(minimum, std::min(maximum, value));
}

bool parseVoiceMode(
    const std::string& value,
    auto_unicom_voice::DeliveryMode& mode
) {
    const std::string normalized = upperAscii(trim(value));
    if (normalized == "OFF") {
        mode = auto_unicom_voice::DeliveryMode::Off;
        return true;
    }
    if (normalized == "LOCAL" || normalized == "LOCAL_READBACK") {
        mode = auto_unicom_voice::DeliveryMode::LocalReadback;
        return true;
    }
    if (normalized == "RADIO") {
        mode = auto_unicom_voice::DeliveryMode::Radio;
        return true;
    }
    return false;
}

void normalizeConfig(HelperConfig& config) {
    config.autoUnicomMode = trim(config.autoUnicomMode);
    config.altitudeCallsign = upperAscii(trim(config.altitudeCallsign));
    config.autoUnicomFrequencyKhz = clampValue(
        config.autoUnicomFrequencyKhz, kFrequencyMinKhz, kFrequencyMaxKhz);
    config.altitudeWindowTitle = trim(config.altitudeWindowTitle);
    if (config.altitudeWindowTitle.empty()) {
        config.altitudeWindowTitle = "IVAO Pilot Client: Altitude";
    }
    config.autoUnicomMessageFieldName = trim(config.autoUnicomMessageFieldName);
    if (config.autoUnicomMessageFieldName.empty()) {
        config.autoUnicomMessageFieldName = "Message";
    }
    config.autoUnicomSendButtonText = trim(config.autoUnicomSendButtonText);
    if (config.autoUnicomSendButtonText.empty()) {
        config.autoUnicomSendButtonText = "SEND";
    }
    config.autoUnicomConfirmTimeoutMs = clampValue(
        config.autoUnicomConfirmTimeoutMs, kConfirmTimeoutMinMs, kConfirmTimeoutMaxMs);
    config.autoUnicomGateMaxAgeMs = clampValue(
        config.autoUnicomGateMaxAgeMs, kGateMaxAgeMinMs, kGateMaxAgeMaxMs);
    config.autoUnicomFinalGateTimeoutMs = clampValue(
        config.autoUnicomFinalGateTimeoutMs,
        kFinalGateTimeoutMinMs,
        kFinalGateTimeoutMaxMs);
    config.uiaRetryMs = clampValue(config.uiaRetryMs, kUiaRetryMinMs, kUiaRetryMaxMs);

    config.altitudeAudioInput = trim(config.altitudeAudioInput);
    config.altitudeAudioOutput = trim(config.altitudeAudioOutput);
    config.altitudeAudioInputMatch = trim(config.altitudeAudioInputMatch);
    config.altitudeAudioOutputMatch = trim(config.altitudeAudioOutputMatch);

    config.autoUnicomChimeFile = trim(config.autoUnicomChimeFile);
    if (config.autoUnicomChimeFile.empty()) {
        config.autoUnicomChimeFile = "resources/auto_unicom_chime.wav";
    }
    config.autoUnicomVoiceOutput = trim(config.autoUnicomVoiceOutput);
    config.autoUnicomVoiceOutputMatch = trim(config.autoUnicomVoiceOutputMatch);
    config.autoUnicomVoiceLocalOutput = trim(config.autoUnicomVoiceLocalOutput);
    config.autoUnicomVoiceLocalOutputMatch = trim(config.autoUnicomVoiceLocalOutputMatch);
    config.autoUnicomVoiceSapiVoice = trim(config.autoUnicomVoiceSapiVoice);
    config.autoUnicomVoiceSapiRate = clampValue(
        config.autoUnicomVoiceSapiRate, kVoiceRateMin, kVoiceRateMax);
    config.autoUnicomVoiceVolume = clampValue(config.autoUnicomVoiceVolume, 0, 100);
    config.autoUnicomVoicePttLeadMs = clampValue(
        config.autoUnicomVoicePttLeadMs, 0, kVoiceLeadMaxMs);
    config.autoUnicomVoicePttTailMs = clampValue(
        config.autoUnicomVoicePttTailMs, 0, kVoiceTailMaxMs);
    config.autoUnicomVoiceReceiveWaitMs = clampValue(
        config.autoUnicomVoiceReceiveWaitMs, 0, kVoiceReceiveWaitMaxMs);
    config.autoUnicomVoiceReceiveQuietMs = clampValue(
        config.autoUnicomVoiceReceiveQuietMs, 0, kVoiceReceiveQuietMaxMs);
    config.autoUnicomVoicePttConfirmMs = clampValue(
        config.autoUnicomVoicePttConfirmMs,
        kVoiceConfirmMinMs,
        kVoiceConfirmMaxMs);
    config.autoUnicomVoiceTestText = trim(config.autoUnicomVoiceTestText);
    if (config.autoUnicomVoiceTestText.empty()) {
        config.autoUnicomVoiceTestText = "Auto Unicom voice audio test";
    }
    config.pttKey = normalizeKeyName(config.pttKey);
    if (config.pttKey.empty()) {
        config.pttKey = "LCTRL";
    }
}

} // namespace

ConfigLoadResult loadHelperConfigFile(
    const std::filesystem::path& path,
    HelperConfig& outConfig
) {
    std::ifstream in(path);
    if (!in.is_open()) {
        return {false, true, "configuration file not found", {}};
    }

    HelperConfig candidate;
    std::vector<std::string> unknownKeys;
    std::string line;
    int lineNumber = 0;
    try {
        while (std::getline(in, line)) {
            ++lineNumber;
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            const std::string stripped = trim(line);
            if (stripped.empty() || stripped.front() == '#' || stripped.front() == ';') {
                continue;
            }
            const std::size_t separator = stripped.find('=');
            if (separator == std::string::npos) {
                throw std::invalid_argument("missing '='");
            }
            const std::string key = upperAscii(trim(stripped.substr(0, separator)));
            const std::string value = stripped.substr(separator + 1);

            if (key == "AUTO_UNICOM_MODE") {
                const std::string mode = trim(value);
                if (mode != "off" && mode != "dry_run" && mode != "send") {
                    throw std::invalid_argument("AUTO_UNICOM_MODE must be off, dry_run or send");
                }
                candidate.autoUnicomMode = mode;
            } else if (key == "ALTITUDE_CALLSIGN" || key == "NETWORK_CALLSIGN") {
                candidate.altitudeCallsign = value;
            } else if (key == "AUTO_UNICOM_FREQUENCY_KHZ") {
                candidate.autoUnicomFrequencyKhz = parseInt(value);
            } else if (key == "ALTITUDE_WINDOW_TITLE") {
                candidate.altitudeWindowTitle = value;
            } else if (key == "AUTO_UNICOM_MESSAGE_FIELD_NAME") {
                candidate.autoUnicomMessageFieldName = value;
            } else if (key == "AUTO_UNICOM_SEND_BUTTON_TEXT") {
                candidate.autoUnicomSendButtonText = value;
            } else if (key == "AUTO_UNICOM_CONFIRM_TIMEOUT_MS") {
                candidate.autoUnicomConfirmTimeoutMs = parseInt(value);
            } else if (key == "AUTO_UNICOM_GATE_MAX_AGE_MS") {
                candidate.autoUnicomGateMaxAgeMs = parseInt(value);
            } else if (key == "AUTO_UNICOM_FINAL_GATE_TIMEOUT_MS") {
                candidate.autoUnicomFinalGateTimeoutMs = parseInt(value);
            } else if (key == "UIA_RETRY_MS") {
                candidate.uiaRetryMs = parseInt(value);
            } else if (key == "DEBUG_UIA") {
                candidate.debugUia = parseBool(value);
            } else if (key == "ALTITUDE_AUDIO_GUARD") {
                candidate.altitudeAudioGuard = parseBool(value);
            } else if (key == "ALTITUDE_AUDIO_INPUT") {
                candidate.altitudeAudioInput = value;
            } else if (key == "ALTITUDE_AUDIO_OUTPUT") {
                candidate.altitudeAudioOutput = value;
            } else if (key == "ALTITUDE_AUDIO_INPUT_MATCH") {
                candidate.altitudeAudioInputMatch = value;
            } else if (key == "ALTITUDE_AUDIO_OUTPUT_MATCH") {
                candidate.altitudeAudioOutputMatch = value;
            } else if (key == "AUTO_UNICOM_CHIME") {
                candidate.autoUnicomChime = parseBool(value);
            } else if (key == "AUTO_UNICOM_CHIME_FILE") {
                candidate.autoUnicomChimeFile = value;
            } else if (key == "AUTO_UNICOM_VOICE_MODE") {
                if (!parseVoiceMode(value, candidate.autoUnicomVoiceMode)) {
                    throw std::invalid_argument("AUTO_UNICOM_VOICE_MODE must be off, local or radio");
                }
            } else if (key == "AUTO_UNICOM_VOICE_OUTPUT") {
                candidate.autoUnicomVoiceOutput = value;
            } else if (key == "AUTO_UNICOM_VOICE_OUTPUT_MATCH") {
                candidate.autoUnicomVoiceOutputMatch = value;
            } else if (key == "AUTO_UNICOM_VOICE_LOCAL_OUTPUT") {
                candidate.autoUnicomVoiceLocalOutput = value;
            } else if (key == "AUTO_UNICOM_VOICE_LOCAL_OUTPUT_MATCH") {
                candidate.autoUnicomVoiceLocalOutputMatch = value;
            } else if (key == "AUTO_UNICOM_VOICE_SAPI_VOICE") {
                candidate.autoUnicomVoiceSapiVoice = value;
            } else if (key == "AUTO_UNICOM_VOICE_SAPI_RATE") {
                candidate.autoUnicomVoiceSapiRate = parseInt(value);
            } else if (key == "AUTO_UNICOM_VOICE_VOLUME") {
                candidate.autoUnicomVoiceVolume = parseInt(value);
            } else if (key == "AUTO_UNICOM_VOICE_PTT_LEAD_MS") {
                candidate.autoUnicomVoicePttLeadMs = parseInt(value);
            } else if (key == "AUTO_UNICOM_VOICE_PTT_TAIL_MS") {
                candidate.autoUnicomVoicePttTailMs = parseInt(value);
            } else if (key == "AUTO_UNICOM_VOICE_RECEIVE_WAIT_MS") {
                candidate.autoUnicomVoiceReceiveWaitMs = parseInt(value);
            } else if (key == "AUTO_UNICOM_VOICE_RECEIVE_QUIET_MS") {
                candidate.autoUnicomVoiceReceiveQuietMs = parseInt(value);
            } else if (key == "AUTO_UNICOM_VOICE_PTT_CONFIRM_MS") {
                candidate.autoUnicomVoicePttConfirmMs = parseInt(value);
            } else if (key == "AUTO_UNICOM_VOICE_TEST_TEXT") {
                candidate.autoUnicomVoiceTestText = value;
            } else if (key == "PTT_KEY") {
                candidate.pttKey = value;
            } else {
                unknownKeys.push_back(key);
            }
        }
        normalizeConfig(candidate);
        if (!isSupportedPttKey(candidate.pttKey)) {
            throw std::invalid_argument("unsupported PTT_KEY");
        }
    } catch (const std::exception& ex) {
        return {
            false,
            false,
            "line " + std::to_string(lineNumber) + ": " + ex.what(),
            std::move(unknownKeys),
        };
    }

    outConfig = std::move(candidate);
    return {true, false, {}, std::move(unknownKeys)};
}

bool saveHelperConfigFile(
    const std::filesystem::path& path,
    const HelperConfig& config,
    std::string& error
) {
    std::error_code directoryError;
    std::filesystem::create_directories(path.parent_path(), directoryError);
    if (directoryError) {
        error = "could not create preferences directory: " + directoryError.message();
        return false;
    }
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        error = "could not open configuration for writing";
        return false;
    }

    out << "# YAL Auto-Unicom Helper\n";
    out << "# Productive transport is intentionally disabled by default.\n";
    out << "AUTO_UNICOM_MODE=" << config.autoUnicomMode << "\n";
    out << "ALTITUDE_CALLSIGN=" << config.altitudeCallsign << "\n";
    out << "AUTO_UNICOM_FREQUENCY_KHZ=" << config.autoUnicomFrequencyKhz << "\n";
    out << "ALTITUDE_WINDOW_TITLE=" << config.altitudeWindowTitle << "\n";
    out << "AUTO_UNICOM_MESSAGE_FIELD_NAME=" << config.autoUnicomMessageFieldName << "\n";
    out << "AUTO_UNICOM_SEND_BUTTON_TEXT=" << config.autoUnicomSendButtonText << "\n";
    out << "AUTO_UNICOM_CONFIRM_TIMEOUT_MS=" << config.autoUnicomConfirmTimeoutMs << "\n";
    out << "AUTO_UNICOM_GATE_MAX_AGE_MS=" << config.autoUnicomGateMaxAgeMs << "\n";
    out << "AUTO_UNICOM_FINAL_GATE_TIMEOUT_MS=" << config.autoUnicomFinalGateTimeoutMs << "\n";
    out << "UIA_RETRY_MS=" << config.uiaRetryMs << "\n";
    out << "DEBUG_UIA=" << (config.debugUia ? 1 : 0) << "\n";
    out << "ALTITUDE_AUDIO_GUARD=" << (config.altitudeAudioGuard ? 1 : 0) << "\n";
    out << "ALTITUDE_AUDIO_INPUT=" << config.altitudeAudioInput << "\n";
    out << "ALTITUDE_AUDIO_OUTPUT=" << config.altitudeAudioOutput << "\n";
    out << "ALTITUDE_AUDIO_INPUT_MATCH=" << config.altitudeAudioInputMatch << "\n";
    out << "ALTITUDE_AUDIO_OUTPUT_MATCH=" << config.altitudeAudioOutputMatch << "\n";
    out << "AUTO_UNICOM_CHIME=" << (config.autoUnicomChime ? 1 : 0) << "\n";
    out << "AUTO_UNICOM_CHIME_FILE=" << config.autoUnicomChimeFile << "\n";
    out << "AUTO_UNICOM_VOICE_MODE="
        << auto_unicom_voice::deliveryModeName(config.autoUnicomVoiceMode) << "\n";
    out << "AUTO_UNICOM_VOICE_OUTPUT=" << config.autoUnicomVoiceOutput << "\n";
    out << "AUTO_UNICOM_VOICE_OUTPUT_MATCH=" << config.autoUnicomVoiceOutputMatch << "\n";
    out << "AUTO_UNICOM_VOICE_LOCAL_OUTPUT=" << config.autoUnicomVoiceLocalOutput << "\n";
    out << "AUTO_UNICOM_VOICE_LOCAL_OUTPUT_MATCH="
        << config.autoUnicomVoiceLocalOutputMatch << "\n";
    out << "AUTO_UNICOM_VOICE_SAPI_VOICE=" << config.autoUnicomVoiceSapiVoice << "\n";
    out << "AUTO_UNICOM_VOICE_SAPI_RATE=" << config.autoUnicomVoiceSapiRate << "\n";
    out << "AUTO_UNICOM_VOICE_VOLUME=" << config.autoUnicomVoiceVolume << "\n";
    out << "AUTO_UNICOM_VOICE_PTT_LEAD_MS=" << config.autoUnicomVoicePttLeadMs << "\n";
    out << "AUTO_UNICOM_VOICE_PTT_TAIL_MS=" << config.autoUnicomVoicePttTailMs << "\n";
    out << "AUTO_UNICOM_VOICE_RECEIVE_WAIT_MS="
        << config.autoUnicomVoiceReceiveWaitMs << "\n";
    out << "AUTO_UNICOM_VOICE_RECEIVE_QUIET_MS="
        << config.autoUnicomVoiceReceiveQuietMs << "\n";
    out << "AUTO_UNICOM_VOICE_PTT_CONFIRM_MS="
        << config.autoUnicomVoicePttConfirmMs << "\n";
    out << "AUTO_UNICOM_VOICE_TEST_TEXT=" << config.autoUnicomVoiceTestText << "\n";
    out << "PTT_KEY=" << config.pttKey << "\n";

    if (!out.good()) {
        error = "configuration write failed";
        return false;
    }
    error.clear();
    return true;
}

std::vector<std::string> diffHelperConfig(
    const HelperConfig& left,
    const HelperConfig& right
) {
    std::vector<std::string> changed;
#define CONFIG_DIFF(field, key) if (left.field != right.field) changed.emplace_back(key)
    CONFIG_DIFF(autoUnicomMode, "AUTO_UNICOM_MODE");
    CONFIG_DIFF(altitudeCallsign, "ALTITUDE_CALLSIGN");
    CONFIG_DIFF(autoUnicomFrequencyKhz, "AUTO_UNICOM_FREQUENCY_KHZ");
    CONFIG_DIFF(altitudeWindowTitle, "ALTITUDE_WINDOW_TITLE");
    CONFIG_DIFF(autoUnicomMessageFieldName, "AUTO_UNICOM_MESSAGE_FIELD_NAME");
    CONFIG_DIFF(autoUnicomSendButtonText, "AUTO_UNICOM_SEND_BUTTON_TEXT");
    CONFIG_DIFF(autoUnicomConfirmTimeoutMs, "AUTO_UNICOM_CONFIRM_TIMEOUT_MS");
    CONFIG_DIFF(autoUnicomGateMaxAgeMs, "AUTO_UNICOM_GATE_MAX_AGE_MS");
    CONFIG_DIFF(autoUnicomFinalGateTimeoutMs, "AUTO_UNICOM_FINAL_GATE_TIMEOUT_MS");
    CONFIG_DIFF(uiaRetryMs, "UIA_RETRY_MS");
    CONFIG_DIFF(debugUia, "DEBUG_UIA");
    CONFIG_DIFF(altitudeAudioGuard, "ALTITUDE_AUDIO_GUARD");
    CONFIG_DIFF(altitudeAudioInput, "ALTITUDE_AUDIO_INPUT");
    CONFIG_DIFF(altitudeAudioOutput, "ALTITUDE_AUDIO_OUTPUT");
    CONFIG_DIFF(altitudeAudioInputMatch, "ALTITUDE_AUDIO_INPUT_MATCH");
    CONFIG_DIFF(altitudeAudioOutputMatch, "ALTITUDE_AUDIO_OUTPUT_MATCH");
    CONFIG_DIFF(autoUnicomChime, "AUTO_UNICOM_CHIME");
    CONFIG_DIFF(autoUnicomChimeFile, "AUTO_UNICOM_CHIME_FILE");
    CONFIG_DIFF(autoUnicomVoiceMode, "AUTO_UNICOM_VOICE_MODE");
    CONFIG_DIFF(autoUnicomVoiceOutput, "AUTO_UNICOM_VOICE_OUTPUT");
    CONFIG_DIFF(autoUnicomVoiceOutputMatch, "AUTO_UNICOM_VOICE_OUTPUT_MATCH");
    CONFIG_DIFF(autoUnicomVoiceLocalOutput, "AUTO_UNICOM_VOICE_LOCAL_OUTPUT");
    CONFIG_DIFF(autoUnicomVoiceLocalOutputMatch, "AUTO_UNICOM_VOICE_LOCAL_OUTPUT_MATCH");
    CONFIG_DIFF(autoUnicomVoiceSapiVoice, "AUTO_UNICOM_VOICE_SAPI_VOICE");
    CONFIG_DIFF(autoUnicomVoiceSapiRate, "AUTO_UNICOM_VOICE_SAPI_RATE");
    CONFIG_DIFF(autoUnicomVoiceVolume, "AUTO_UNICOM_VOICE_VOLUME");
    CONFIG_DIFF(autoUnicomVoicePttLeadMs, "AUTO_UNICOM_VOICE_PTT_LEAD_MS");
    CONFIG_DIFF(autoUnicomVoicePttTailMs, "AUTO_UNICOM_VOICE_PTT_TAIL_MS");
    CONFIG_DIFF(autoUnicomVoiceReceiveWaitMs, "AUTO_UNICOM_VOICE_RECEIVE_WAIT_MS");
    CONFIG_DIFF(autoUnicomVoiceReceiveQuietMs, "AUTO_UNICOM_VOICE_RECEIVE_QUIET_MS");
    CONFIG_DIFF(autoUnicomVoicePttConfirmMs, "AUTO_UNICOM_VOICE_PTT_CONFIRM_MS");
    CONFIG_DIFF(autoUnicomVoiceTestText, "AUTO_UNICOM_VOICE_TEST_TEXT");
    CONFIG_DIFF(pttKey, "PTT_KEY");
#undef CONFIG_DIFF
    return changed;
}

std::string normalizeKeyName(const std::string& value) {
    std::string normalized = upperAscii(trim(value));
    normalized.erase(std::remove_if(normalized.begin(), normalized.end(), [](char ch) {
        return ch == ' ' || ch == '-' || ch == '_';
    }), normalized.end());
    return normalized;
}

bool isSupportedPttKey(const std::string& value) {
    const std::string key = normalizeKeyName(value);
    static const std::unordered_set<std::string> namedKeys = {
        "CTRL", "CONTROL", "LCTRL", "LCONTROL", "LEFTCTRL", "LEFTCONTROL",
        "RCTRL", "RCONTROL", "RIGHTCTRL", "RIGHTCONTROL",
        "LSHIFT", "LEFTSHIFT", "RSHIFT", "RIGHTSHIFT",
        "LALT", "LEFTALT", "RALT", "RIGHTALT",
    };
    if (namedKeys.count(key) != 0) {
        return true;
    }
    if (key.size() == 1 && std::isalnum(static_cast<unsigned char>(key.front()))) {
        return true;
    }
    if (key.size() >= 2 && key.front() == 'F') {
        try {
            const int number = std::stoi(key.substr(1));
            return number >= 1 && number <= 24;
        } catch (...) {
            return false;
        }
    }
    return false;
}

AutoUnicomControlModes getAutoUnicomControlModes(const HelperConfig& config) {
    return {config.autoUnicomMode, config.autoUnicomVoiceMode};
}

bool isAutoUnicomTextEnabled(const AutoUnicomControlModes& modes) {
    return modes.textMode == "send";
}

void setAutoUnicomTextEnabled(AutoUnicomControlModes& modes, bool enabled) {
    modes.textMode = enabled ? "send" : "off";
    if (!enabled) {
        modes.voiceMode = auto_unicom_voice::DeliveryMode::Off;
    }
}

void setAutoUnicomVoiceMode(
    AutoUnicomControlModes& modes,
    auto_unicom_voice::DeliveryMode mode
) {
    modes.voiceMode = mode;
    if (mode != auto_unicom_voice::DeliveryMode::Off) {
        modes.textMode = "send";
    }
}
