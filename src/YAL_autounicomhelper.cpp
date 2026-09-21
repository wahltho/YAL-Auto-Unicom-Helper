#include "altitude_audio_guard.h"
#include "auto_unicom.h"
#include "auto_unicom_voice.h"
#include "helper_config.h"
#include "pcm_sound.h"
#include "pilotui_message_sender.h"

#include "XPLMDataAccess.h"
#include "XPLMMenus.h"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMUtilities.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if IBM
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

constexpr const char* kPluginName = "YAL_autounicomhelper";
constexpr const char* kPluginSignature = "yal.autounicomhelper";
constexpr const char* kPluginDescription =
    "Auto-Unicom transport helper for YAL and IVAO Altitude v0.1.0";
constexpr const char* kPluginVersion = "0.1.0b1";
constexpr const char* kAltitudePluginSignature = "aero.ivao.altitude";
constexpr const char* kDataRefPrefix = "wahltho/autounicom/";
constexpr float kFlightLoopIntervalSec = 0.05f;
constexpr int kVoiceStatusFreshMs = 250;
constexpr int kComposerRecoveryRetryMs = 5000;
constexpr int kFrequencyMinKhz = 100000;
constexpr int kFrequencyMaxKhz = 200000;

HelperConfig g_config;
std::filesystem::path g_pluginRoot;
std::filesystem::path g_configPath;
std::filesystem::path g_logPath;
std::filesystem::path g_altitudeConfigPath;
std::ofstream g_log;
std::mutex g_logMutex;
std::chrono::steady_clock::time_point g_altitudeAudioGuardNextCheck{};

std::atomic<bool> g_pluginEnabled{false};
std::atomic<bool> g_apiReady{false};
std::atomic<int> g_transportState{
    static_cast<int>(auto_unicom::TransportState::Unavailable)};

auto_unicom::Mailbox g_mailbox;
std::thread g_worker;
std::atomic<bool> g_workerRunning{false};
std::atomic<bool> g_workerStop{false};
std::atomic<long long> g_composerRecoveryRetryAfterMs{0};

std::mutex g_gateMutex;
std::condition_variable g_gateCv;
auto_unicom::GateSnapshot g_gateSnapshot;
std::chrono::steady_clock::time_point g_gateUpdatedAt{};
unsigned long long g_gateGeneration = 0;

std::atomic<bool> g_altitudePresent{false};
std::atomic<bool> g_pttStatusKnown{false};
std::atomic<bool> g_pttActive{false};
std::atomic<bool> g_receiveStatusKnown{false};
std::atomic<bool> g_receiveActive{false};
std::atomic<long long> g_voiceStatusUpdatedMs{0};
std::atomic<bool> g_voiceContextAllowed{false};
std::mutex g_pttMutex;
bool g_pttOwned = false;

std::mutex g_voiceResultMutex;
int g_voiceResultSequence = 0;
auto_unicom_voice::ResultCode g_voiceResultCode = auto_unicom_voice::ResultCode::Idle;
std::string g_voiceResultDetail = "IDLE";
std::atomic<int> g_voiceState{static_cast<int>(auto_unicom_voice::State::Disabled)};

bool g_reloadPending = false;
std::optional<AutoUnicomControlModes> g_pendingControlModes;
std::string g_pendingControlSource;
std::atomic<bool> g_chimePending{false};
std::atomic<bool> g_chimeTestPending{false};
PcmSound g_chimeSound;

XPLMDataRef g_drOnline = nullptr;
XPLMDataRef g_drPtt = nullptr;
XPLMDataRef g_drCom1Rx = nullptr;
XPLMDataRef g_drCom2Rx = nullptr;
XPLMDataRef g_drAudioPanelOut = nullptr;
XPLMDataRef g_drCom1Frequency833 = nullptr;
XPLMDataRef g_drCom2Frequency833 = nullptr;
XPLMDataRef g_drCom1FrequencyLegacy = nullptr;
XPLMDataRef g_drCom2FrequencyLegacy = nullptr;

std::vector<XPLMDataRef> g_ownedDataRefs;
std::vector<std::pair<XPLMCommandRef, intptr_t>> g_commands;

XPLMMenuID g_menu = nullptr;
XPLMMenuID g_autoUnicomMenu = nullptr;
XPLMMenuID g_voiceMenu = nullptr;
int g_pluginsMenuItem = -1;
int g_autoUnicomMenuItem = -1;
int g_textMenuItem = -1;
int g_voiceMenuItem = -1;
int g_voiceOffMenuItem = -1;
int g_voiceLocalMenuItem = -1;
int g_voiceRadioMenuItem = -1;

enum CommandId : intptr_t {
    kCommandTextToggle = 1,
    kCommandTextOn,
    kCommandTextOff,
    kCommandVoiceOff,
    kCommandVoiceLocal,
    kCommandVoiceRadio,
    kCommandVoiceTest,
    kCommandChimeTest,
    kCommandDiscover,
    kCommandReloadConfig,
};

void logLine(const std::string& message) {
    const std::string line = "[YAL Auto-Unicom Helper] " + message + "\n";
    XPLMDebugString(line.c_str());
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_log.is_open()) {
        g_log << line;
        g_log.flush();
    }
}

long long steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::filesystem::path xPlaneRoot() {
    char path[4096] = {};
    XPLMGetSystemPath(path);
    return std::filesystem::path(path);
}

std::filesystem::path resolvePluginRoot() {
    char path[4096] = {};
    XPLMGetPluginInfo(XPLMGetMyID(), nullptr, path, nullptr, nullptr);
    std::filesystem::path binaryPath(path);
    if (binaryPath.has_parent_path()) {
        binaryPath = binaryPath.parent_path();
    }
    if (binaryPath.filename() == "64" && binaryPath.has_parent_path()) {
        binaryPath = binaryPath.parent_path();
    }
    return binaryPath;
}

auto_unicom::Mode configuredMode(const HelperConfig& config) {
    if (config.autoUnicomMode == "send") {
        return auto_unicom::Mode::Send;
    }
    if (config.autoUnicomMode == "dry_run") {
        return auto_unicom::Mode::DryRun;
    }
    return auto_unicom::Mode::Off;
}

altitude_audio_guard::Config configuredAltitudeAudioGuard(const HelperConfig& config) {
    return {
        config.altitudeAudioGuard,
        config.altitudeAudioInput,
        config.altitudeAudioOutput,
        config.altitudeAudioInputMatch,
        config.altitudeAudioOutputMatch,
    };
}

auto_unicom_voice::Config configuredRadioVoice(const HelperConfig& config) {
    auto_unicom_voice::Config voice{};
    voice.enabled = config.autoUnicomVoiceMode == auto_unicom_voice::DeliveryMode::Radio;
    voice.outputDeviceId = config.autoUnicomVoiceOutput;
    voice.outputDeviceMatch = config.autoUnicomVoiceOutputMatch;
    voice.sapiVoiceName = config.autoUnicomVoiceSapiVoice;
    voice.sapiRate = config.autoUnicomVoiceSapiRate;
    voice.volume = config.autoUnicomVoiceVolume;
    voice.pttLeadMs = config.autoUnicomVoicePttLeadMs;
    voice.pttTailMs = config.autoUnicomVoicePttTailMs;
    voice.receiveWaitMs = config.autoUnicomVoiceReceiveWaitMs;
    voice.receiveQuietMs = config.autoUnicomVoiceReceiveQuietMs;
    voice.pttConfirmMs = config.autoUnicomVoicePttConfirmMs;
    return voice;
}

auto_unicom_voice::Config configuredLocalVoice(const HelperConfig& config) {
    auto voice = configuredRadioVoice(config);
    voice.outputDeviceId = config.autoUnicomVoiceLocalOutput;
    voice.outputDeviceMatch = config.autoUnicomVoiceLocalOutputMatch;
    voice.allowDefaultOutput = true;
    return voice;
}

void publishVoiceResult(
    int sequence,
    auto_unicom_voice::ResultCode code,
    std::string detail
) {
    std::lock_guard<std::mutex> lock(g_voiceResultMutex);
    g_voiceResultSequence = sequence;
    g_voiceResultCode = code;
    g_voiceResultDetail = detail.empty()
        ? auto_unicom_voice::resultCodeName(code)
        : std::move(detail);
}

int copyStringData(const std::string& source, void* outValue, int offset, int maxBytes) {
    if (offset < 0) {
        return 0;
    }
    const int length = static_cast<int>(source.size());
    if (!outValue) {
        return length;
    }
    if (offset >= length || maxBytes <= 0) {
        return 0;
    }
    const int count = std::min(maxBytes, length - offset);
    std::memcpy(outValue, source.data() + offset, static_cast<std::size_t>(count));
    return count;
}

int normalizeComFrequencyKhz(int raw) {
    if (raw >= kFrequencyMinKhz && raw <= kFrequencyMaxKhz) {
        return raw;
    }
    if (raw >= 10000 && raw <= 20000) {
        return raw * 10;
    }
    return 0;
}

bool getActiveComFrequencyKhz(int& frequencyKhz, int& activeCom) {
    frequencyKhz = 0;
    activeCom = 0;
    if (!g_drAudioPanelOut) {
        return false;
    }
    const int selector = XPLMGetDatai(g_drAudioPanelOut);
    if (selector != 6 && selector != 7) {
        return false;
    }
    activeCom = selector == 6 ? 1 : 2;
    XPLMDataRef frequency = activeCom == 1 ? g_drCom1Frequency833 : g_drCom2Frequency833;
    if (!frequency) {
        frequency = activeCom == 1 ? g_drCom1FrequencyLegacy : g_drCom2FrequencyLegacy;
    }
    if (!frequency) {
        return false;
    }
    frequencyKhz = normalizeComFrequencyKhz(XPLMGetDatai(frequency));
    return frequencyKhz != 0;
}

int virtualKeyFromName(const std::string& raw) {
#if IBM
    const std::string key = normalizeKeyName(raw);
    if (key == "LCTRL" || key == "LCONTROL" || key == "LEFTCTRL" ||
        key == "LEFTCONTROL" || key == "CTRL" || key == "CONTROL") {
        return VK_LCONTROL;
    }
    if (key == "RCTRL" || key == "RCONTROL" || key == "RIGHTCTRL" ||
        key == "RIGHTCONTROL") {
        return VK_RCONTROL;
    }
    if (key == "LSHIFT" || key == "LEFTSHIFT") return VK_LSHIFT;
    if (key == "RSHIFT" || key == "RIGHTSHIFT") return VK_RSHIFT;
    if (key == "LALT" || key == "LEFTALT") return VK_LMENU;
    if (key == "RALT" || key == "RIGHTALT") return VK_RMENU;
    if (key.size() == 1 && ((key[0] >= 'A' && key[0] <= 'Z') ||
        (key[0] >= '0' && key[0] <= '9'))) {
        return static_cast<int>(key[0]);
    }
    if (key.size() >= 2 && key.front() == 'F') {
        try {
            const int number = std::stoi(key.substr(1));
            if (number >= 1 && number <= 24) {
                return VK_F1 + number - 1;
            }
        } catch (...) {
        }
    }
#else
    (void)raw;
#endif
    return 0;
}

bool sendVirtualKey(int key, bool down) {
#if IBM
    if (key == 0) {
        return false;
    }
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = static_cast<WORD>(key);
    if (!down) {
        input.ki.dwFlags = KEYEVENTF_KEYUP;
    }
    return SendInput(1, &input, sizeof(input)) == 1;
#else
    (void)key;
    (void)down;
    return false;
#endif
}

bool voiceStatusFresh() {
    const long long updated = g_voiceStatusUpdatedMs.load();
    const long long age = steadyNowMs() - updated;
    return updated > 0 && age >= 0 && age <= kVoiceStatusFreshMs;
}

bool acquireVoicePtt(const HelperConfig& config) {
    std::lock_guard<std::mutex> lock(g_pttMutex);
    if (!g_altitudePresent.load() || g_pttOwned || !voiceStatusFresh() ||
        !g_receiveStatusKnown.load() || g_receiveActive.load()) {
        return false;
    }
    const int key = virtualKeyFromName(config.pttKey);
    if (key == 0 || !sendVirtualKey(key, true)) {
        return false;
    }
    g_pttOwned = true;
    return true;
}

void releaseVoicePtt(const HelperConfig& config) {
    std::lock_guard<std::mutex> lock(g_pttMutex);
    if (!g_pttOwned) {
        return;
    }
    const int key = virtualKeyFromName(config.pttKey);
    if (key == 0 || !sendVirtualKey(key, false)) {
        logLine("Auto UNICOM voice: PTT release failed");
    }
    g_pttOwned = false;
}

void bindRuntimeDataRefs() {
    const bool altitudePresent =
        XPLMFindPluginBySignature(kAltitudePluginSignature) != XPLM_NO_PLUGIN_ID;
    if (altitudePresent != g_altitudePresent.load()) {
        logLine(std::string("Altitude plugin ") +
            (altitudePresent ? "detected" : "not found"));
    }
    g_altitudePresent.store(altitudePresent);
    if (!altitudePresent) {
        g_drOnline = nullptr;
        g_drPtt = nullptr;
        g_drCom1Rx = nullptr;
        g_drCom2Rx = nullptr;
        g_pttStatusKnown.store(false);
        g_receiveStatusKnown.store(false);
        g_pttActive.store(false);
        g_receiveActive.store(false);
        g_voiceStatusUpdatedMs.store(0);
        return;
    }
    if (!g_drOnline) g_drOnline = XPLMFindDataRef("ivaopilot/online");
    if (!g_drPtt) g_drPtt = XPLMFindDataRef("ivaopilot/ptt");
    if (!g_drCom1Rx) g_drCom1Rx = XPLMFindDataRef(auto_unicom_voice::kCom1ReceiveDataRef);
    if (!g_drCom2Rx) g_drCom2Rx = XPLMFindDataRef(auto_unicom_voice::kCom2ReceiveDataRef);
}

void sampleVoiceStatus(int activeCom) {
    const bool runtimeAvailable = g_pluginEnabled.load() && g_altitudePresent.load();
    g_pttStatusKnown.store(runtimeAvailable && g_drPtt != nullptr);
    g_pttActive.store(runtimeAvailable && g_drPtt && XPLMGetDatai(g_drPtt) != 0);
    XPLMDataRef receiveRef = runtimeAvailable && activeCom == 1 ? g_drCom1Rx :
        (runtimeAvailable && activeCom == 2 ? g_drCom2Rx : nullptr);
    g_receiveStatusKnown.store(receiveRef != nullptr);
    g_receiveActive.store(receiveRef && XPLMGetDatai(receiveRef) != 0);
    g_voiceStatusUpdatedMs.store(steadyNowMs());
}

void updateGateState() {
    auto_unicom::GateSnapshot snapshot{};
    snapshot.mode = configuredMode(g_config);
    snapshot.initialized = g_apiReady.load() && g_pluginEnabled.load();
#if IBM
    snapshot.platformSupported = true;
#else
    snapshot.platformSupported = false;
#endif
    snapshot.pilotUiAvailable = g_altitudePresent.load();
    snapshot.onlineKnown = g_drOnline != nullptr;
    snapshot.online = snapshot.onlineKnown && XPLMGetDatai(g_drOnline) != 0;
    snapshot.frequencyKnown = getActiveComFrequencyKhz(
        snapshot.activeFrequencyKhz,
        snapshot.activeCom);
    sampleVoiceStatus(snapshot.activeCom);
    snapshot.unicomFrequencyKhz = g_config.autoUnicomFrequencyKhz;
    snapshot.busy = g_mailbox.busy() || g_workerRunning.load() ||
        pilotui_message::hasOwnedComposerDraft();
    snapshot.ageMs = 0;
    snapshot.maxAgeMs = g_config.autoUnicomGateMaxAgeMs;

    const auto decision = auto_unicom::evaluateGate(snapshot);
    g_transportState.store(static_cast<int>(decision.state));
    g_voiceContextAllowed.store(auto_unicom::evaluateGate(snapshot, false).allowed);
    {
        std::lock_guard<std::mutex> lock(g_gateMutex);
        g_gateSnapshot = snapshot;
        g_gateUpdatedAt = std::chrono::steady_clock::now();
        ++g_gateGeneration;
    }
    g_gateCv.notify_all();
}

void ensureAltitudeAudioConfig(bool verbose) {
    altitude_audio_guard::ensureConfig(
        g_altitudeConfigPath,
        configuredAltitudeAudioGuard(g_config),
        verbose,
        [](const std::string& line) { logLine(line); });
}

void maybeEnsureAltitudeAudioConfig() {
    if (!g_config.altitudeAudioGuard) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (g_altitudeAudioGuardNextCheck != std::chrono::steady_clock::time_point{} &&
        now < g_altitudeAudioGuardNextCheck) {
        return;
    }
    g_altitudeAudioGuardNextCheck =
        now + std::chrono::seconds(altitude_audio_guard::kCheckIntervalSeconds);
    ensureAltitudeAudioConfig(false);
}

auto_unicom::GateSnapshot cachedGate() {
    std::lock_guard<std::mutex> lock(g_gateMutex);
    auto snapshot = g_gateSnapshot;
    if (g_gateUpdatedAt == std::chrono::steady_clock::time_point{}) {
        snapshot.ageMs = snapshot.maxAgeMs + 1;
    } else {
        snapshot.ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - g_gateUpdatedAt).count();
    }
    return snapshot;
}

auto_unicom::GateDecision waitForFreshGate(int timeoutMs) {
    std::unique_lock<std::mutex> lock(g_gateMutex);
    const unsigned long long generation = g_gateGeneration;
    const bool updated = g_gateCv.wait_for(
        lock,
        std::chrono::milliseconds(timeoutMs),
        [&]() { return g_gateGeneration > generation || g_workerStop.load(); });
    if (g_workerStop.load()) {
        return {false, auto_unicom::TransportState::Error,
            auto_unicom::ResultCode::Cancelled, "CANCELLED"};
    }
    if (!updated) {
        return {false, auto_unicom::TransportState::Error,
            auto_unicom::ResultCode::FailedBeforeSubmit, "GATE_REFRESH_TIMEOUT"};
    }
    auto snapshot = g_gateSnapshot;
    snapshot.ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - g_gateUpdatedAt).count();
    return auto_unicom::evaluateGate(snapshot, false);
}

auto_unicom_voice::Result playLocalReadback(
    const auto_unicom::Request& request,
    const HelperConfig& config
) {
    auto_unicom_voice::Callbacks callbacks{};
    callbacks.log = [](const std::string& line) { logLine(line); };
    callbacks.shouldStop = []() { return g_workerStop.load() || !g_pluginEnabled.load(); };
    callbacks.stateChanged = [](auto_unicom_voice::State state) {
        g_voiceState.store(static_cast<int>(state));
    };
    return auto_unicom_voice::playAudioTest(
        configuredLocalVoice(config),
        request.voiceText,
        callbacks);
}

auto_unicom_voice::Result transmitRadioVoice(
    const auto_unicom::Request& request,
    const HelperConfig& config
) {
    auto_unicom_voice::Callbacks callbacks{};
    callbacks.log = [](const std::string& line) { logLine(line); };
    callbacks.shouldStop = []() { return g_workerStop.load() || !g_pluginEnabled.load(); };
    callbacks.contextAllowed = []() { return g_voiceContextAllowed.load(); };
    callbacks.finalGate = [&](std::string& detail) {
        const auto decision = waitForFreshGate(config.autoUnicomFinalGateTimeoutMs);
        detail = decision.detail;
        return decision.allowed;
    };
    callbacks.receiveStatusKnown = []() {
        return voiceStatusFresh() && g_receiveStatusKnown.load();
    };
    callbacks.receiveActive = []() { return g_receiveActive.load(); };
    callbacks.pttStatusKnown = []() {
        return voiceStatusFresh() && g_pttStatusKnown.load();
    };
    callbacks.pttActive = []() { return g_pttActive.load(); };
    callbacks.pttDown = [&]() { return acquireVoicePtt(config); };
    callbacks.pttUp = [&]() { releaseVoicePtt(config); };
    callbacks.stateChanged = [](auto_unicom_voice::State state) {
        g_voiceState.store(static_cast<int>(state));
    };
    return auto_unicom_voice::transmit(
        configuredRadioVoice(config),
        request.voiceText,
        callbacks);
}

void autoUnicomWorker(auto_unicom::Request request, HelperConfig config) {
    auto finish = [&](auto_unicom::ResultCode code, const std::string& detail) {
        if ((request.channels & auto_unicom_voice::kChannelVoice) != 0) {
            publishVoiceResult(
                request.sequence,
                auto_unicom_voice::ResultCode::Cancelled,
                "VOICE_NOT_RUN:" + detail);
        }
        g_mailbox.publish(request.sequence, code, detail);
        logLine("Auto UNICOM: seq=" + std::to_string(request.sequence) +
            " result=" + auto_unicom::resultCodeName(code) + " detail=" + detail);
        g_workerRunning.store(false);
    };

    auto_unicom::GateDecision finalGateDecision{};
    pilotui_message::Options options{};
    options.windowTitle = config.altitudeWindowTitle;
    options.composerName = config.autoUnicomMessageFieldName;
    options.sendButtonName = config.autoUnicomSendButtonText;
    options.message = request.text;
    options.confirmTimeoutMs = config.autoUnicomConfirmTimeoutMs;
    options.pollMs = config.uiaRetryMs;
    options.debug = config.debugUia;

    pilotui_message::Callbacks callbacks{};
    callbacks.log = [](const std::string& line) { logLine(line); };
    callbacks.shouldStop = []() { return g_workerStop.load() || !g_pluginEnabled.load(); };
    callbacks.finalGate = [&](std::string& detail) {
        finalGateDecision = waitForFreshGate(config.autoUnicomFinalGateTimeoutMs);
        detail = finalGateDecision.detail;
        return finalGateDecision.allowed;
    };

    pilotui_message::Result result{};
    try {
        result = pilotui_message::submitActiveFrequencyMessage(options, callbacks);
    } catch (const std::exception& ex) {
        result = {pilotui_message::Status::UncertainAfterSubmit,
            std::string("SENDER_EXCEPTION:") + ex.what()};
    } catch (...) {
        result = {pilotui_message::Status::UncertainAfterSubmit, "SENDER_EXCEPTION"};
    }

    switch (result.status) {
    case pilotui_message::Status::SubmittedVisible:
        g_mailbox.publish(
            request.sequence,
            auto_unicom::ResultCode::SubmittedVisible,
            result.detail);
        logLine("Auto UNICOM: seq=" + std::to_string(request.sequence) +
            " result=SUBMITTED_VISIBLE detail=" + result.detail);
        if ((request.channels & auto_unicom_voice::kChannelVoice) != 0) {
            if (config.autoUnicomVoiceMode == auto_unicom_voice::DeliveryMode::Off) {
                g_voiceState.store(static_cast<int>(auto_unicom_voice::State::Disabled));
                publishVoiceResult(
                    request.sequence,
                    auto_unicom_voice::ResultCode::Disabled,
                    "VOICE_DISABLED");
            } else if (config.autoUnicomVoiceMode ==
                auto_unicom_voice::DeliveryMode::LocalReadback) {
                const auto voiceResult = playLocalReadback(request, config);
                publishVoiceResult(
                    request.sequence,
                    voiceResult.code == auto_unicom_voice::ResultCode::Transmitted
                        ? auto_unicom_voice::ResultCode::Disabled
                        : voiceResult.code,
                    voiceResult.code == auto_unicom_voice::ResultCode::Transmitted
                        ? "VOICE_LOCAL_READBACK_PLAYED"
                        : voiceResult.detail);
            } else {
                const auto voiceResult = transmitRadioVoice(request, config);
                publishVoiceResult(request.sequence, voiceResult.code, voiceResult.detail);
            }
        }
        g_chimePending.store(true);
        g_workerRunning.store(false);
        break;
    case pilotui_message::Status::RejectedByGate:
        finish(
            finalGateDecision.rejectionCode == auto_unicom::ResultCode::Idle
                ? auto_unicom::ResultCode::FailedBeforeSubmit
                : finalGateDecision.rejectionCode,
            result.detail);
        break;
    case pilotui_message::Status::Cancelled:
        finish(auto_unicom::ResultCode::Cancelled, result.detail);
        break;
    case pilotui_message::Status::UncertainAfterSubmit:
        finish(auto_unicom::ResultCode::UncertainAfterSubmit, result.detail);
        break;
    case pilotui_message::Status::Unsupported:
    case pilotui_message::Status::FailedBeforeSubmit:
    default:
        finish(auto_unicom::ResultCode::FailedBeforeSubmit, result.detail);
        break;
    }
}

void composerRecoveryWorker(HelperConfig config) {
    pilotui_message::Options options{};
    options.windowTitle = config.altitudeWindowTitle;
    options.composerName = config.autoUnicomMessageFieldName;
    options.sendButtonName = config.autoUnicomSendButtonText;
    options.pollMs = config.uiaRetryMs;
    options.debug = config.debugUia;

    pilotui_message::Callbacks callbacks{};
    callbacks.log = [](const std::string& line) { logLine(line); };
    callbacks.shouldStop = []() { return g_workerStop.load() || !g_pluginEnabled.load(); };

    pilotui_message::RecoveryResult result{};
    try {
        result = pilotui_message::recoverOwnedComposerDraft(options, callbacks);
    } catch (const std::exception& ex) {
        result = {pilotui_message::RecoveryStatus::Failed,
            std::string("RECOVERY_EXCEPTION:") + ex.what()};
    } catch (...) {
        result = {pilotui_message::RecoveryStatus::Failed, "RECOVERY_EXCEPTION"};
    }
    logLine("Auto UNICOM composer recovery: " + result.detail);
    g_composerRecoveryRetryAfterMs.store(steadyNowMs() + kComposerRecoveryRetryMs);
    g_workerRunning.store(false);
}

void startComposerRecovery() {
    if (g_workerRunning.load()) {
        return;
    }
    if (g_worker.joinable()) {
        g_worker.join();
    }
    g_workerStop.store(false);
    g_workerRunning.store(true);
    logLine("Auto UNICOM composer recovery: starting");
    g_worker = std::thread(composerRecoveryWorker, g_config);
}

void discoveryWorker(HelperConfig config) {
    pilotui_message::Options options{};
    options.windowTitle = config.altitudeWindowTitle;
    options.composerName = config.autoUnicomMessageFieldName;
    options.sendButtonName = config.autoUnicomSendButtonText;
    options.debug = true;
    pilotui_message::Callbacks callbacks{};
    callbacks.log = [](const std::string& line) { logLine(line); };
    callbacks.shouldStop = []() { return g_workerStop.load() || !g_pluginEnabled.load(); };
    try {
        const auto result = pilotui_message::discoverActiveFrequencyControls(options, callbacks);
        logLine("Auto UNICOM discovery: " +
            std::string(result.success ? "success " : "failed ") + result.detail);
    } catch (const std::exception& ex) {
        logLine(std::string("Auto UNICOM discovery: exception ") + ex.what());
    } catch (...) {
        logLine("Auto UNICOM discovery: exception");
    }
    g_workerRunning.store(false);
}

void voiceTestWorker(HelperConfig config) {
    auto_unicom_voice::Callbacks callbacks{};
    callbacks.log = [](const std::string& line) { logLine(line); };
    callbacks.shouldStop = []() { return g_workerStop.load() || !g_pluginEnabled.load(); };
    callbacks.stateChanged = [](auto_unicom_voice::State state) {
        g_voiceState.store(static_cast<int>(state));
    };
    const auto result = auto_unicom_voice::playAudioTest(
        configuredLocalVoice(config),
        config.autoUnicomVoiceTestText,
        callbacks);
    logLine("Auto UNICOM voice test: result=" +
        std::string(auto_unicom_voice::resultCodeName(result.code)) +
        " detail=" + result.detail);
    g_voiceState.store(static_cast<int>(
        config.autoUnicomVoiceMode == auto_unicom_voice::DeliveryMode::Off
            ? auto_unicom_voice::State::Disabled
            : auto_unicom_voice::State::Ready));
    g_workerRunning.store(false);
}

template <typename Function>
bool startWorker(const char* label, Function function) {
    if (g_workerRunning.load() || g_mailbox.busy() ||
        pilotui_message::hasOwnedComposerDraft()) {
        logLine(std::string(label) + ": busy");
        return false;
    }
    if (g_worker.joinable()) {
        g_worker.join();
    }
    g_workerStop.store(false);
    g_workerRunning.store(true);
    g_worker = std::thread(std::move(function));
    return true;
}

void processPendingRequest() {
    if (!g_apiReady.load() || g_workerRunning.load()) {
        return;
    }
    if (g_worker.joinable()) {
        g_worker.join();
    }
    if (pilotui_message::hasOwnedComposerDraft()) {
        if (pilotui_message::ownedComposerRecoveryDue(g_config.autoUnicomComposerStaleMs) &&
            steadyNowMs() >= g_composerRecoveryRetryAfterMs.load()) {
            startComposerRecovery();
        }
        return;
    }
    g_composerRecoveryRetryAfterMs.store(0);
    auto request = g_mailbox.takePending();
    if (!request) {
        return;
    }

    const bool voiceRequested =
        (request->channels & auto_unicom_voice::kChannelVoice) != 0;
    publishVoiceResult(
        request->sequence,
        voiceRequested
            ? auto_unicom_voice::ResultCode::Accepted
            : auto_unicom_voice::ResultCode::NotRequested,
        voiceRequested ? "ACCEPTED" : "VOICE_NOT_REQUESTED");

    auto voiceValidation = auto_unicom_voice::validateRequest(
        request->channels,
        request->voiceText,
        request->voiceInputOverflow);
    if (!voiceValidation.valid) {
        g_mailbox.publish(
            request->sequence,
            voiceValidation.voiceRequested
                ? auto_unicom::ResultCode::RejectedText
                : auto_unicom::ResultCode::RejectedPolicy,
            voiceValidation.detail);
        publishVoiceResult(
            request->sequence,
            voiceValidation.voiceRequested
                ? auto_unicom_voice::ResultCode::RejectedText
                : auto_unicom_voice::ResultCode::RejectedContext,
            voiceValidation.detail);
        return;
    }
    request->voiceText = std::move(voiceValidation.normalizedVoiceText);

    auto validation = auto_unicom::validateMessageText(
        request->text,
        request->inputOverflow);
    if (!validation.valid) {
        g_mailbox.publish(
            request->sequence,
            auto_unicom::ResultCode::RejectedText,
            validation.detail);
        if (voiceRequested) {
            publishVoiceResult(
                request->sequence,
                auto_unicom_voice::ResultCode::Cancelled,
                "VOICE_NOT_RUN:" + validation.detail);
        }
        return;
    }
    request->text = std::move(validation.normalized);

    const auto mode = configuredMode(g_config);
    if (mode == auto_unicom::Mode::Off) {
        g_mailbox.publish(
            request->sequence,
            auto_unicom::ResultCode::RejectedPolicy,
            "MODE_OFF");
        if (voiceRequested) {
            publishVoiceResult(
                request->sequence,
                auto_unicom_voice::ResultCode::Disabled,
                "VOICE_NOT_RUN:MODE_OFF");
        }
        return;
    }
    if (mode == auto_unicom::Mode::DryRun) {
        g_mailbox.publish(
            request->sequence,
            auto_unicom::ResultCode::PreviewReady,
            "PREVIEW_READY");
        if (voiceRequested) {
            publishVoiceResult(
                request->sequence,
                auto_unicom_voice::ResultCode::Disabled,
                "VOICE_NOT_RUN:DRY_RUN");
        }
        logLine("Auto UNICOM: dry-run seq=" + std::to_string(request->sequence) +
            " text=" + request->text);
        return;
    }

    auto snapshot = cachedGate();
    snapshot.busy = g_workerRunning.load();
    const auto decision = auto_unicom::evaluateGate(snapshot);
    if (!decision.allowed) {
        g_mailbox.publish(request->sequence, decision.rejectionCode, decision.detail);
        if (voiceRequested) {
            publishVoiceResult(
                request->sequence,
                auto_unicom_voice::ResultCode::RejectedContext,
                "VOICE_NOT_RUN:" + decision.detail);
        }
        return;
    }

    const HelperConfig config = g_config;
    const auto sequence = request->sequence;
    const std::string text = request->text;
    if (g_worker.joinable()) {
        g_worker.join();
    }
    g_workerStop.store(false);
    g_workerRunning.store(true);
    logLine("Auto UNICOM: starting transaction seq=" + std::to_string(sequence) +
        " voice_mode=" + auto_unicom_voice::deliveryModeName(config.autoUnicomVoiceMode) +
        " text=" + text);
    g_worker = std::thread(autoUnicomWorker, std::move(*request), config);
}

void updateChime() {
    const bool test = g_chimeTestPending.exchange(false);
    const bool success = g_chimePending.exchange(false);
    if (!test && (!success || !g_config.autoUnicomChime)) {
        return;
    }
    if (!ensurePcmSoundLoaded(
            g_pluginRoot,
            g_config.autoUnicomChimeFile,
            g_chimeSound,
            logLine)) {
        return;
    }
    if (!playPcmSound(g_chimeSound)) {
        logLine("Auto UNICOM chime: playback failed");
    }
}

void updateMenuItems() {
    if (!g_autoUnicomMenu || !g_voiceMenu) {
        return;
    }
    const auto modes = g_pendingControlModes
        ? *g_pendingControlModes
        : getAutoUnicomControlModes(g_config);
    const char* textLabel = modes.textMode == "dry_run" ? "TEXT (DRY RUN)" : "TEXT";
    XPLMSetMenuItemName(g_autoUnicomMenu, g_textMenuItem, textLabel, 1);
    XPLMCheckMenuItem(
        g_autoUnicomMenu,
        g_textMenuItem,
        isAutoUnicomTextEnabled(modes) ? xplm_Menu_Checked : xplm_Menu_Unchecked);
    XPLMCheckMenuItem(
        g_voiceMenu,
        g_voiceOffMenuItem,
        modes.voiceMode == auto_unicom_voice::DeliveryMode::Off
            ? xplm_Menu_Checked : xplm_Menu_Unchecked);
    XPLMCheckMenuItem(
        g_voiceMenu,
        g_voiceLocalMenuItem,
        modes.voiceMode == auto_unicom_voice::DeliveryMode::LocalReadback
            ? xplm_Menu_Checked : xplm_Menu_Unchecked);
    XPLMCheckMenuItem(
        g_voiceMenu,
        g_voiceRadioMenuItem,
        modes.voiceMode == auto_unicom_voice::DeliveryMode::Radio
            ? xplm_Menu_Checked : xplm_Menu_Unchecked);
}

AutoUnicomControlModes requestedControlModes() {
    return g_pendingControlModes
        ? *g_pendingControlModes
        : getAutoUnicomControlModes(g_config);
}

void requestControlModes(AutoUnicomControlModes modes, const char* source) {
    g_pendingControlModes = std::move(modes);
    g_pendingControlSource = source ? source : "unknown";
    updateMenuItems();
}

void requestTextEnabled(bool enabled, const char* source) {
    auto modes = requestedControlModes();
    setAutoUnicomTextEnabled(modes, enabled);
    requestControlModes(std::move(modes), source);
}

void requestVoiceMode(auto_unicom_voice::DeliveryMode mode, const char* source) {
    auto modes = requestedControlModes();
    setAutoUnicomVoiceMode(modes, mode);
    requestControlModes(std::move(modes), source);
}

void applyPendingControlModes() {
    if (!g_pendingControlModes || g_workerRunning.load() || g_mailbox.busy()) {
        return;
    }
    g_config.autoUnicomMode = g_pendingControlModes->textMode;
    g_config.autoUnicomVoiceMode = g_pendingControlModes->voiceMode;
    const std::string source = g_pendingControlSource;
    g_pendingControlModes.reset();
    g_pendingControlSource.clear();
    std::string error;
    if (!saveHelperConfigFile(g_configPath, g_config, error)) {
        logLine("Config save failed: " + error);
    }
    g_voiceState.store(static_cast<int>(
        g_config.autoUnicomVoiceMode == auto_unicom_voice::DeliveryMode::Off
            ? auto_unicom_voice::State::Disabled
            : auto_unicom_voice::State::Ready));
    updateMenuItems();
    logLine("Controls applied source=" + source +
        " text=" + g_config.autoUnicomMode +
        " voice=" + auto_unicom_voice::deliveryModeName(g_config.autoUnicomVoiceMode));
}

void processConfigReload() {
    if (!g_reloadPending || g_workerRunning.load() || g_mailbox.busy()) {
        return;
    }
    g_reloadPending = false;
    HelperConfig candidate;
    const auto result = loadHelperConfigFile(g_configPath, candidate);
    if (!result.ok) {
        logLine("Config reload failed: " + result.error);
        return;
    }
    const auto changed = diffHelperConfig(g_config, candidate);
    if (candidate.autoUnicomChimeFile != g_config.autoUnicomChimeFile) {
        resetPcmSound(g_chimeSound);
    }
    g_config = std::move(candidate);
    g_altitudeAudioGuardNextCheck = {};
    g_pendingControlModes.reset();
    g_voiceState.store(static_cast<int>(
        g_config.autoUnicomVoiceMode == auto_unicom_voice::DeliveryMode::Off
            ? auto_unicom_voice::State::Disabled
            : auto_unicom_voice::State::Ready));
    updateMenuItems();
    std::ostringstream message;
    message << "Config reloaded changed=" << changed.size();
    for (const auto& key : changed) {
        message << " " << key;
    }
    logLine(message.str());
    for (const auto& key : result.unknownKeys) {
        logLine("Config: ignored unknown key " + key);
    }
    ensureAltitudeAudioConfig(true);
}

void stopWorker(const char* reason) {
    g_workerStop.store(true);
    g_gateCv.notify_all();
    if (g_worker.joinable()) {
        g_worker.join();
    }
    releaseVoicePtt(g_config);
    g_workerRunning.store(false);
    g_mailbox.cancelActive(std::string("CANCELLED:") + reason);
    g_chimePending.store(false);
    g_chimeTestPending.store(false);
}

int getApiVersion(void*) { return auto_unicom::kApiVersion; }
int getReady(void*) { return g_apiReady.load() ? 1 : 0; }
int getMode(void*) { return static_cast<int>(configuredMode(g_config)); }
int getTransportState(void*) { return g_transportState.load(); }
int getEffectiveCallsign(void*, void* out, int offset, int maxBytes) {
    return copyStringData(
        auto_unicom::selectEffectiveCallsign(g_config.altitudeCallsign, {}),
        out,
        offset,
        maxBytes);
}
int getRequestText(void*, void* out, int offset, int maxBytes) {
    return copyStringData(g_mailbox.requestText(), out, offset, maxBytes);
}
void setRequestText(void*, void* value, int offset, int byteCount) {
    g_mailbox.writeRequestText(value, offset, byteCount);
}
int getRequestSequence(void*) { return g_mailbox.requestSequence(); }
void setRequestSequence(void*, int sequence) {
    const auto result = g_mailbox.commit(sequence);
    switch (result) {
    case auto_unicom::CommitResult::Accepted:
        logLine("Auto UNICOM: request accepted seq=" + std::to_string(sequence));
        break;
    case auto_unicom::CommitResult::DuplicateOrOld:
        logLine("Auto UNICOM: request ignored duplicate/old seq=" + std::to_string(sequence));
        break;
    case auto_unicom::CommitResult::Busy:
        logLine("Auto UNICOM: request rejected busy seq=" + std::to_string(sequence));
        break;
    case auto_unicom::CommitResult::InvalidSequence:
        logLine("Auto UNICOM: request rejected invalid seq=" + std::to_string(sequence));
        break;
    }
}
int getResultSequence(void*) { return g_mailbox.resultSequence(); }
int getResultCode(void*) { return static_cast<int>(g_mailbox.resultCode()); }
int getResultDetail(void*, void* out, int offset, int maxBytes) {
    return copyStringData(g_mailbox.resultDetail(), out, offset, maxBytes);
}
int getRequestChannels(void*) { return g_mailbox.requestChannels(); }
void setRequestChannels(void*, int channels) { g_mailbox.setRequestChannels(channels); }
int getRequestVoiceText(void*, void* out, int offset, int maxBytes) {
    return copyStringData(g_mailbox.requestVoiceText(), out, offset, maxBytes);
}
void setRequestVoiceText(void*, void* value, int offset, int byteCount) {
    g_mailbox.writeRequestVoiceText(value, offset, byteCount);
}
int getVoiceState(void*) { return g_voiceState.load(); }
int getVoiceResultSequence(void*) {
    std::lock_guard<std::mutex> lock(g_voiceResultMutex);
    return g_voiceResultSequence;
}
int getVoiceResultCode(void*) {
    std::lock_guard<std::mutex> lock(g_voiceResultMutex);
    return static_cast<int>(g_voiceResultCode);
}
int getVoiceResultDetail(void*, void* out, int offset, int maxBytes) {
    std::lock_guard<std::mutex> lock(g_voiceResultMutex);
    return copyStringData(g_voiceResultDetail, out, offset, maxBytes);
}

XPLMDataRef registerIntDataRef(
    const std::string& suffix,
    bool writable,
    XPLMGetDatai_f read,
    XPLMSetDatai_f write
) {
    XPLMDataRef dataRef = XPLMRegisterDataAccessor(
        (std::string(kDataRefPrefix) + suffix).c_str(),
        xplmType_Int,
        writable ? 1 : 0,
        read,
        write,
        nullptr, nullptr,
        nullptr, nullptr,
        nullptr, nullptr,
        nullptr, nullptr,
        nullptr, nullptr,
        nullptr, nullptr);
    if (dataRef) {
        g_ownedDataRefs.push_back(dataRef);
    }
    return dataRef;
}

XPLMDataRef registerByteDataRef(
    const std::string& suffix,
    bool writable,
    XPLMGetDatab_f read,
    XPLMSetDatab_f write
) {
    XPLMDataRef dataRef = XPLMRegisterDataAccessor(
        (std::string(kDataRefPrefix) + suffix).c_str(),
        xplmType_Data,
        writable ? 1 : 0,
        nullptr, nullptr,
        nullptr, nullptr,
        nullptr, nullptr,
        nullptr, nullptr,
        nullptr, nullptr,
        read, write,
        nullptr, nullptr);
    if (dataRef) {
        g_ownedDataRefs.push_back(dataRef);
    }
    return dataRef;
}

void registerApiDataRefs() {
    bool ok = true;
    ok &= registerIntDataRef("api_version", false, getApiVersion, nullptr) != nullptr;
    ok &= registerIntDataRef("ready", false, getReady, nullptr) != nullptr;
    ok &= registerIntDataRef("mode", false, getMode, nullptr) != nullptr;
    ok &= registerIntDataRef("transport_state", false, getTransportState, nullptr) != nullptr;
    ok &= registerByteDataRef("effective_callsign", false, getEffectiveCallsign, nullptr) != nullptr;
    ok &= registerByteDataRef("request_text", true, getRequestText, setRequestText) != nullptr;
    ok &= registerIntDataRef("request_seq", true, getRequestSequence, setRequestSequence) != nullptr;
    ok &= registerIntDataRef("result_seq", false, getResultSequence, nullptr) != nullptr;
    ok &= registerIntDataRef("result_code", false, getResultCode, nullptr) != nullptr;
    ok &= registerByteDataRef("result_detail", false, getResultDetail, nullptr) != nullptr;
    ok &= registerIntDataRef("request_channels", true, getRequestChannels, setRequestChannels) != nullptr;
    ok &= registerByteDataRef(
        "request_voice_text", true, getRequestVoiceText, setRequestVoiceText) != nullptr;
    ok &= registerIntDataRef("voice_state", false, getVoiceState, nullptr) != nullptr;
    ok &= registerIntDataRef(
        "voice_result_seq", false, getVoiceResultSequence, nullptr) != nullptr;
    ok &= registerIntDataRef(
        "voice_result_code", false, getVoiceResultCode, nullptr) != nullptr;
    ok &= registerByteDataRef(
        "voice_result_detail", false, getVoiceResultDetail, nullptr) != nullptr;
    g_apiReady.store(ok);
    logLine(std::string("Auto UNICOM API v3: ") +
        (ok ? "ready" : "registration failed") +
        " prefix=" + kDataRefPrefix);
}

void unregisterApiDataRefs() {
    g_apiReady.store(false);
    for (auto it = g_ownedDataRefs.rbegin(); it != g_ownedDataRefs.rend(); ++it) {
        XPLMUnregisterDataAccessor(*it);
    }
    g_ownedDataRefs.clear();
}

int commandHandler(XPLMCommandRef, XPLMCommandPhase phase, void* refcon) {
    if (phase != xplm_CommandBegin) {
        return 0;
    }
    const auto command = static_cast<CommandId>(reinterpret_cast<intptr_t>(refcon));
    switch (command) {
    case kCommandTextToggle:
        requestTextEnabled(!isAutoUnicomTextEnabled(requestedControlModes()), "command:text_toggle");
        break;
    case kCommandTextOn:
        requestTextEnabled(true, "command:text_on");
        break;
    case kCommandTextOff:
        requestTextEnabled(false, "command:text_off");
        break;
    case kCommandVoiceOff:
        requestVoiceMode(auto_unicom_voice::DeliveryMode::Off, "command:voice_off");
        break;
    case kCommandVoiceLocal:
        requestVoiceMode(auto_unicom_voice::DeliveryMode::LocalReadback, "command:voice_local");
        break;
    case kCommandVoiceRadio:
        requestVoiceMode(auto_unicom_voice::DeliveryMode::Radio, "command:voice_radio");
        break;
    case kCommandVoiceTest:
        startWorker("Auto UNICOM voice test", [config = g_config]() { voiceTestWorker(config); });
        break;
    case kCommandChimeTest:
        g_chimeTestPending.store(true);
        break;
    case kCommandDiscover:
        startWorker("Auto UNICOM discovery", [config = g_config]() { discoveryWorker(config); });
        break;
    case kCommandReloadConfig:
        g_reloadPending = true;
        logLine("Config reload requested");
        break;
    }
    return 1;
}

void registerCommand(const char* name, const char* description, CommandId id) {
    XPLMCommandRef command = XPLMCreateCommand(name, description);
    if (!command) {
        logLine(std::string("Command registration failed: ") + name);
        return;
    }
    XPLMRegisterCommandHandler(
        command,
        commandHandler,
        1,
        reinterpret_cast<void*>(static_cast<intptr_t>(id)));
    g_commands.emplace_back(command, static_cast<intptr_t>(id));
}

void registerCommands() {
    registerCommand("yal_autounicomhelper/autounicom_text_toggle",
        "Toggle Auto-Unicom text transport", kCommandTextToggle);
    registerCommand("yal_autounicomhelper/autounicom_text_on",
        "Enable Auto-Unicom text transport", kCommandTextOn);
    registerCommand("yal_autounicomhelper/autounicom_text_off",
        "Disable Auto-Unicom text and voice transport", kCommandTextOff);
    registerCommand("yal_autounicomhelper/autounicom_voice_off",
        "Disable Auto-Unicom voice", kCommandVoiceOff);
    registerCommand("yal_autounicomhelper/autounicom_voice_local",
        "Enable local Auto-Unicom voice readback", kCommandVoiceLocal);
    registerCommand("yal_autounicomhelper/autounicom_voice_radio",
        "Enable Auto-Unicom radio voice", kCommandVoiceRadio);
    registerCommand("yal_autounicomhelper/autounicom_voice_audio_test",
        "Play local Auto-Unicom voice test without PTT", kCommandVoiceTest);
    registerCommand("yal_autounicomhelper/autounicom_chime_test",
        "Play Auto-Unicom success chime", kCommandChimeTest);
    registerCommand("yal_autounicomhelper/autounicom_discover",
        "Discover Altitude message controls without writing", kCommandDiscover);
    registerCommand("yal_autounicomhelper/reload_config",
        "Reload YAL Auto-Unicom Helper configuration", kCommandReloadConfig);
}

void unregisterCommands() {
    for (const auto& entry : g_commands) {
        XPLMUnregisterCommandHandler(
            entry.first,
            commandHandler,
            1,
            reinterpret_cast<void*>(entry.second));
    }
    g_commands.clear();
}

void menuHandler(void*, void* itemRef) {
    const auto item = static_cast<CommandId>(reinterpret_cast<intptr_t>(itemRef));
    commandHandler(nullptr, xplm_CommandBegin, reinterpret_cast<void*>(static_cast<intptr_t>(item)));
}

void createMenus() {
    g_pluginsMenuItem = XPLMAppendMenuItem(
        XPLMFindPluginsMenu(),
        "YAL Auto-Unicom Helper",
        nullptr,
        1);
    g_menu = XPLMCreateMenu(
        "YAL Auto-Unicom Helper",
        XPLMFindPluginsMenu(),
        g_pluginsMenuItem,
        menuHandler,
        nullptr);
    if (!g_menu) {
        logLine("Menu creation failed");
        return;
    }
    g_autoUnicomMenuItem = XPLMAppendMenuItem(g_menu, "AUTO UNICOM", nullptr, 1);
    g_autoUnicomMenu = XPLMCreateMenu(
        "AUTO UNICOM",
        g_menu,
        g_autoUnicomMenuItem,
        menuHandler,
        nullptr);
    if (!g_autoUnicomMenu) {
        return;
    }
    g_textMenuItem = XPLMAppendMenuItem(
        g_autoUnicomMenu,
        "TEXT",
        reinterpret_cast<void*>(static_cast<intptr_t>(kCommandTextToggle)),
        1);
    g_voiceMenuItem = XPLMAppendMenuItem(g_autoUnicomMenu, "VOICE", nullptr, 1);
    g_voiceMenu = XPLMCreateMenu(
        "VOICE",
        g_autoUnicomMenu,
        g_voiceMenuItem,
        menuHandler,
        nullptr);
    if (g_voiceMenu) {
        g_voiceOffMenuItem = XPLMAppendMenuItem(
            g_voiceMenu, "OFF",
            reinterpret_cast<void*>(static_cast<intptr_t>(kCommandVoiceOff)), 1);
        g_voiceLocalMenuItem = XPLMAppendMenuItem(
            g_voiceMenu, "LOCAL READBACK",
            reinterpret_cast<void*>(static_cast<intptr_t>(kCommandVoiceLocal)), 1);
        g_voiceRadioMenuItem = XPLMAppendMenuItem(
            g_voiceMenu, "RADIO",
            reinterpret_cast<void*>(static_cast<intptr_t>(kCommandVoiceRadio)), 1);
    }
    XPLMAppendMenuSeparator(g_menu);
    XPLMAppendMenuItem(
        g_menu, "Voice Audio Test",
        reinterpret_cast<void*>(static_cast<intptr_t>(kCommandVoiceTest)), 1);
    XPLMAppendMenuItem(
        g_menu, "Chime Test",
        reinterpret_cast<void*>(static_cast<intptr_t>(kCommandChimeTest)), 1);
    XPLMAppendMenuItem(
        g_menu, "Discover Altitude UI",
        reinterpret_cast<void*>(static_cast<intptr_t>(kCommandDiscover)), 1);
    XPLMAppendMenuSeparator(g_menu);
    XPLMAppendMenuItem(
        g_menu, "Reload Config",
        reinterpret_cast<void*>(static_cast<intptr_t>(kCommandReloadConfig)), 1);
    updateMenuItems();
}

void destroyMenus() {
    if (g_voiceMenu) {
        XPLMDestroyMenu(g_voiceMenu);
        g_voiceMenu = nullptr;
    }
    if (g_autoUnicomMenu) {
        XPLMDestroyMenu(g_autoUnicomMenu);
        g_autoUnicomMenu = nullptr;
    }
    if (g_menu) {
        XPLMDestroyMenu(g_menu);
        g_menu = nullptr;
    }
    if (g_pluginsMenuItem >= 0) {
        XPLMRemoveMenuItem(XPLMFindPluginsMenu(), g_pluginsMenuItem);
        g_pluginsMenuItem = -1;
    }
}

float flightLoopCallback(float, float, int, void*) {
    maybeEnsureAltitudeAudioConfig();
    bindRuntimeDataRefs();
    updateGateState();
    processPendingRequest();
    applyPendingControlModes();
    processConfigReload();
    updateChime();
    if (g_worker.joinable() && !g_workerRunning.load()) {
        g_worker.join();
    }
    return kFlightLoopIntervalSec;
}

void loadInitialConfig() {
    HelperConfig loaded;
    const auto result = loadHelperConfigFile(g_configPath, loaded);
    if (result.ok) {
        g_config = std::move(loaded);
        for (const auto& key : result.unknownKeys) {
            logLine("Config: ignored unknown key " + key);
        }
        return;
    }
    if (!result.fileMissing) {
        logLine("Config load failed; using safe defaults: " + result.error);
        return;
    }
    g_config = HelperConfig{};
    std::string error;
    if (saveHelperConfigFile(g_configPath, g_config, error)) {
        logLine("Created safe default config: " + g_configPath.string());
    } else {
        logLine("Could not create default config: " + error);
    }
}

} // namespace

PLUGIN_API int XPluginStart(char* outName, char* outSignature, char* outDescription) {
    std::strncpy(outName, kPluginName, 255);
    std::strncpy(outSignature, kPluginSignature, 255);
    std::strncpy(outDescription, kPluginDescription, 255);

    g_pluginRoot = resolvePluginRoot();
    const auto root = xPlaneRoot();
    g_configPath = root / "Output" / "preferences" / "YAL_AutoUnicomHelper.prf";
    g_logPath = root / "Output" / "preferences" / "YAL_AutoUnicomHelper.log";
    g_altitudeConfigPath = root / "IVAO_Pilot_Client.conf";
    g_log.open(g_logPath, std::ios::out | std::ios::app);
    logLine(std::string("Starting version ") + kPluginVersion);
    logLine("Plugin root: " + g_pluginRoot.string());
    loadInitialConfig();
    ensureAltitudeAudioConfig(true);

    g_drAudioPanelOut = XPLMFindDataRef("sim/cockpit/switches/audio_panel_out");
    g_drCom1Frequency833 = XPLMFindDataRef(
        "sim/cockpit2/radios/actuators/com1_frequency_hz_833");
    g_drCom2Frequency833 = XPLMFindDataRef(
        "sim/cockpit2/radios/actuators/com2_frequency_hz_833");
    g_drCom1FrequencyLegacy = XPLMFindDataRef("sim/cockpit/radios/com1_freq_hz");
    g_drCom2FrequencyLegacy = XPLMFindDataRef("sim/cockpit/radios/com2_freq_hz");

    registerApiDataRefs();
    registerCommands();
    createMenus();
    g_voiceState.store(static_cast<int>(
        g_config.autoUnicomVoiceMode == auto_unicom_voice::DeliveryMode::Off
            ? auto_unicom_voice::State::Disabled
            : auto_unicom_voice::State::Ready));
    XPLMRegisterFlightLoopCallback(flightLoopCallback, kFlightLoopIntervalSec, nullptr);
    return 1;
}

PLUGIN_API void XPluginStop() {
    g_pluginEnabled.store(false);
    XPLMUnregisterFlightLoopCallback(flightLoopCallback, nullptr);
    stopWorker("PLUGIN_STOP");
    destroyMenus();
    unregisterCommands();
    unregisterApiDataRefs();
    logLine("Stopped");
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_log.is_open()) {
        g_log.close();
    }
}

PLUGIN_API int XPluginEnable() {
    g_pluginEnabled.store(true);
    g_workerStop.store(false);
    logLine("Enabled");
    return 1;
}

PLUGIN_API void XPluginDisable() {
    g_pluginEnabled.store(false);
    stopWorker("PLUGIN_DISABLE");
    g_transportState.store(static_cast<int>(auto_unicom::TransportState::Unavailable));
    logLine("Disabled");
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int, void*) {
}
