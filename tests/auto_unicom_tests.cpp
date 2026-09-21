#include "altitude_audio_guard.h"
#include "auto_unicom.h"
#include "auto_unicom_voice.h"
#include "helper_config.h"
#include "pilotui_message_sender.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void expect(bool condition, const char* label) {
    if (!condition) {
        std::cerr << "FAIL: " << label << "\n";
        ++failures;
    }
}

class TempConfigFile {
public:
    explicit TempConfigFile(const std::string& contents) {
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
            ("yal_autounicomhelper_" + std::to_string(unique) + ".prf");
        std::ofstream out(path);
        out << contents;
    }

    ~TempConfigFile() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }

    std::filesystem::path path;
};

auto_unicom::GateSnapshot readyGate() {
    auto_unicom::GateSnapshot gate{};
    gate.mode = auto_unicom::Mode::Send;
    gate.initialized = true;
    gate.platformSupported = true;
    gate.pilotUiAvailable = true;
    gate.onlineKnown = true;
    gate.online = true;
    gate.frequencyKnown = true;
    gate.activeCom = 1;
    gate.activeFrequencyKhz = 122800;
    gate.unicomFrequencyKhz = 122800;
    gate.ageMs = 10;
    gate.maxAgeMs = 2500;
    return gate;
}

void testEffectiveCallsign() {
    expect(auto_unicom::selectEffectiveCallsign(" dlh3210 ", "BAW123") == "DLH3210",
        "configured callsign has priority");
    expect(auto_unicom::selectEffectiveCallsign("", " baw123 ") == "BAW123",
        "generic fallback remains available");
    expect(auto_unicom::selectEffectiveCallsign("", "").empty(),
        "missing callsign remains empty");
}

void testTextValidation() {
    const auto valid = auto_unicom::validateMessageText("  BER123  taxiing\tvia A B  ");
    expect(valid.valid && valid.normalized == "BER123 taxiing via A B",
        "text whitespace normalized");

    const std::string sasl("Unicom Test\0", 12);
    const auto saslResult = auto_unicom::validateMessageText(sasl);
    expect(saslResult.valid && saslResult.normalized == "Unicom Test",
        "single SASL terminator accepted");

    const std::string embedded("Unicom\0Test", 11);
    expect(auto_unicom::validateMessageText(embedded).detail == "TEXT_NOT_PRINTABLE_ASCII",
        "embedded NUL rejected");
    const std::string multiple("Unicom Test\0\0", 13);
    expect(auto_unicom::validateMessageText(multiple).detail == "TEXT_NOT_PRINTABLE_ASCII",
        "multiple trailing NULs rejected");
    const std::string nulOnly(1, '\0');
    expect(auto_unicom::validateMessageText(nulOnly).detail == "TEXT_EMPTY",
        "NUL-only payload becomes empty");
    expect(auto_unicom::validateMessageText(".metar EDDH").detail == "TEXT_COMMAND_FORBIDDEN",
        "dot command rejected");
    expect(auto_unicom::validateMessageText(std::string(255, 'A')).detail == "TEXT_TOO_LONG",
        "Altitude text limit enforced");
}

void testGate() {
    auto gate = readyGate();
    expect(auto_unicom::evaluateGate(gate).allowed, "online UNICOM allowed");
    gate.online = false;
    expect(auto_unicom::evaluateGate(gate).state == auto_unicom::TransportState::Offline,
        "offline blocked");
    gate = readyGate();
    gate.activeFrequencyKhz = 121500;
    expect(auto_unicom::evaluateGate(gate).state == auto_unicom::TransportState::NotUnicom,
        "non-UNICOM frequency blocked");
    gate = readyGate();
    gate.busy = true;
    expect(auto_unicom::evaluateGate(gate).state == auto_unicom::TransportState::Busy,
        "busy transport blocked");
    expect(auto_unicom::evaluateGate(gate, false).allowed,
        "owned transaction ignores its own busy state");
    gate = readyGate();
    gate.ageMs = 3000;
    expect(auto_unicom::evaluateGate(gate).state == auto_unicom::TransportState::Error,
        "stale gate blocked");
}

void testMailbox() {
    auto_unicom::Mailbox mailbox;
    const std::string text = "BER123 departing runway 23";
    const std::string voice = "Berlin one two three departing runway two three";
    mailbox.writeRequestText(text.data(), 0, static_cast<int>(text.size()));
    mailbox.setRequestChannels(3);
    mailbox.writeRequestVoiceText(voice.data(), 0, static_cast<int>(voice.size()));
    expect(mailbox.commit(1) == auto_unicom::CommitResult::Accepted,
        "first request accepted");
    expect(mailbox.commit(2) == auto_unicom::CommitResult::Busy,
        "parallel request rejected");
    const auto request = mailbox.takePending();
    expect(request && request->sequence == 1 && request->text == text &&
        request->channels == 3 && request->voiceText == voice,
        "mailbox snapshot complete");
    mailbox.publish(1, auto_unicom::ResultCode::SubmittedVisible, "SUBMITTED_VISIBLE");
    expect(!mailbox.busy(), "terminal result releases mailbox");
    expect(mailbox.commit(1) == auto_unicom::CommitResult::DuplicateOrOld,
        "sequence cannot replay");
}

void testVoiceValidationAndReceiveGuard() {
    const auto textOnly = auto_unicom_voice::validateRequest(1, "stale");
    expect(textOnly.valid && !textOnly.voiceRequested, "text-only request ignores voice staging");
    const auto voice = auto_unicom_voice::validateRequest(
        3, "  Berlin one two three\tdeparting  ");
    expect(voice.valid && voice.normalizedVoiceText == "Berlin one two three departing",
        "voice text normalized");
    expect(auto_unicom_voice::validateRequest(2, "voice").detail == "CHANNELS_INVALID",
        "voice-only request forbidden");
    const std::string embedded("Voice\0test", 10);
    expect(auto_unicom_voice::validateRequest(3, embedded).detail ==
        "VOICE_TEXT_NOT_PRINTABLE_ASCII", "embedded voice NUL rejected");

    auto_unicom_voice::ReceiveQuietTracker tracker(1000);
    expect(!tracker.observe(false, 1000), "RX quiet window starts");
    expect(!tracker.observe(true, 1500), "RX activity resets quiet window");
    expect(!tracker.observe(false, 1600), "RX quiet window restarts");
    expect(tracker.observe(false, 2600), "RX quiet window completes");
    expect(std::string_view(auto_unicom_voice::kCom1ReceiveDataRef) ==
        "ivaopilot/audio/com1_rx", "COM1 RX DataRef stable");
    expect(std::string_view(auto_unicom_voice::kCom2ReceiveDataRef) ==
        "ivaopilot/audio/com2_rx", "COM2 RX DataRef stable");
}

void testComposerRetry() {
    using pilotui_message::Result;
    using pilotui_message::Status;
    int attempts = 0;
    int waits = 0;
    pilotui_message::Callbacks callbacks{};
    auto result = pilotui_message::submitWithComposerAmbiguityRetry(
        [&]() {
            ++attempts;
            return attempts == 1
                ? Result{Status::FailedBeforeSubmit, "COMPOSER_AMBIGUOUS"}
                : Result{Status::SubmittedVisible, "SUBMITTED_VISIBLE"};
        },
        [&](int delayMs) {
            ++waits;
            return delayMs == pilotui_message::kComposerAmbiguousRetryDelayMs;
        },
        callbacks);
    expect(attempts == 2 && waits == 1 && result.status == Status::SubmittedVisible,
        "composer ambiguity retried exactly once");

    attempts = 0;
    waits = 0;
    result = pilotui_message::submitWithComposerAmbiguityRetry(
        [&]() {
            ++attempts;
            return Result{Status::UncertainAfterSubmit, "VISIBLE_HISTORY_TIMEOUT"};
        },
        [&](int) { ++waits; return true; },
        callbacks);
    expect(attempts == 1 && waits == 0,
        "uncertain post-submit result never retried");
}

void testComposerOwnershipRecoveryPolicy() {
    using pilotui_message::ComposerOwnershipMatch;
    pilotui_message::ComposerOwnershipTracker tracker;

    expect(!tracker.active() && !tracker.recoveryDue(20000, 15000),
        "inactive composer ownership is never recoverable");
    tracker.beginWrite("BER123 departing runway 23", 1000);
    expect(tracker.active(), "composer ownership starts before write");
    expect(!tracker.recoveryDue(15999, 15000) && tracker.recoveryDue(16000, 15000),
        "composer ownership becomes stale at configured deadline");
    expect(tracker.classify("") == ComposerOwnershipMatch::Empty,
        "empty composer releases ownership");
    expect(tracker.classify("BER123 departing runway 23") == ComposerOwnershipMatch::Exact,
        "exact owned composer is recoverable");
    expect(tracker.classify("BER123 depart") == ComposerOwnershipMatch::Prefix,
        "interrupted write prefix is recoverable");
    expect(tracker.classify("manual message") == ComposerOwnershipMatch::Foreign,
        "foreign composer text is preserved");

    tracker.markComposed();
    expect(tracker.classify("BER123 depart") == ComposerOwnershipMatch::Foreign,
        "pilot edit after complete compose is preserved");
    expect(tracker.classify("BER123 departing runway 23") == ComposerOwnershipMatch::Exact,
        "complete owned draft remains recoverable");

    tracker.clear();
    expect(!tracker.active() &&
        tracker.classify("BER123 depart") == ComposerOwnershipMatch::Foreign,
        "cleared ownership never claims old text");
}

void testConfig() {
    HelperConfig defaults;
    expect(defaults.autoUnicomMode == "off" &&
        defaults.autoUnicomVoiceMode == auto_unicom_voice::DeliveryMode::Off &&
        !defaults.altitudeAudioGuard &&
        defaults.autoUnicomComposerStaleMs == 15000,
        "shipped config is inert");
    expect(defaults.altitudeCallsign.empty(), "callsign fails closed by default");

    TempConfigFile valid(
        "AUTO_UNICOM_MODE=send\n"
        "ALTITUDE_CALLSIGN= dlh3210 \n"
        "AUTO_UNICOM_VOICE_MODE=local\n"
        "AUTO_UNICOM_COMPOSER_STALE_MS=1000\n"
        "AUTO_UNICOM_VOICE_SAPI_RATE=15\n"
        "AUTO_UNICOM_VOICE_VOLUME=-2\n"
        "ALTITUDE_AUDIO_GUARD=1\n"
        "ALTITUDE_AUDIO_INPUT_MATCH= Voicemeeter Out B1 \n"
        "PTT_KEY=left_ctrl\n"
        "FUTURE_KEY=ignored\n");
    HelperConfig config;
    auto result = loadHelperConfigFile(valid.path, config);
    expect(result.ok, "valid config parsed");
    expect(config.altitudeCallsign == "DLH3210", "callsign normalized");
    expect(config.autoUnicomVoiceMode == auto_unicom_voice::DeliveryMode::LocalReadback,
        "local voice parsed");
    expect(config.autoUnicomComposerStaleMs == 5000,
        "composer stale timeout clamped");
    expect(config.autoUnicomVoiceSapiRate == 10 && config.autoUnicomVoiceVolume == 0,
        "voice controls clamped");
    expect(config.altitudeAudioGuard &&
        config.altitudeAudioInputMatch == "Voicemeeter Out B1",
        "audio guard config parsed and normalized");
    expect(config.pttKey == "LEFTCTRL", "PTT key normalized");
    expect(result.unknownKeys.size() == 1 && result.unknownKeys.front() == "FUTURE_KEY",
        "unknown key reported but tolerated");

    HelperConfig changed = config;
    changed.autoUnicomComposerStaleMs = 30000;
    const auto differences = diffHelperConfig(config, changed);
    expect(std::find(differences.begin(), differences.end(),
        "AUTO_UNICOM_COMPOSER_STALE_MS") != differences.end(),
        "composer stale timeout participates in config reload diff");

    TempConfigFile invalid("AUTO_UNICOM_MODE=maybe\n");
    HelperConfig untouched;
    untouched.autoUnicomFrequencyKhz = 130000;
    result = loadHelperConfigFile(invalid.path, untouched);
    expect(!result.ok && untouched.autoUnicomFrequencyKhz == 130000,
        "invalid config is transactional");

    AutoUnicomControlModes modes{"off", auto_unicom_voice::DeliveryMode::Off};
    setAutoUnicomVoiceMode(modes, auto_unicom_voice::DeliveryMode::Radio);
    expect(modes.textMode == "send" &&
        modes.voiceMode == auto_unicom_voice::DeliveryMode::Radio,
        "radio voice enables text");
    setAutoUnicomTextEnabled(modes, false);
    expect(modes.textMode == "off" &&
        modes.voiceMode == auto_unicom_voice::DeliveryMode::Off,
        "text off also disables voice");
}

void testAltitudeAudioGuard() {
    const std::string current =
        "[GENERAL]\nVALUE=keep\n\n[AUDIO]\nINPUT=old-input\nEXTRA=keep\n"
        "OUTPUT=old-output\n\n[NETWORK]\nSERVER=keep\n";
    bool changed = false;
    const std::string updated = altitude_audio_guard::updateConfigText(
        current, "new-input", "new-output", changed);
    expect(changed, "audio guard reports changed audio endpoints");
    expect(updated.find("[GENERAL]\nVALUE=keep") != std::string::npos &&
        updated.find("INPUT=new-input") != std::string::npos &&
        updated.find("OUTPUT=new-output") != std::string::npos &&
        updated.find("EXTRA=keep") != std::string::npos &&
        updated.find("[NETWORK]\nSERVER=keep") != std::string::npos,
        "audio guard changes only INPUT and OUTPUT");

    bool secondChange = true;
    const std::string unchanged = altitude_audio_guard::updateConfigText(
        updated, "new-input", "new-output", secondChange);
    expect(!secondChange && unchanged == updated, "audio guard update is idempotent");

    bool added = false;
    const std::string withAudio = altitude_audio_guard::updateConfigText(
        "[GENERAL]\nVALUE=keep\n", "capture-id", "", added);
    expect(added && withAudio.find("[AUDIO]\nINPUT=capture-id\n") != std::string::npos &&
        withAudio.find("OUTPUT=") == std::string::npos,
        "audio guard creates missing AUDIO section without empty keys");

    expect(altitude_audio_guard::normalizeDeviceId(true, "{device-id}") ==
        "\\\\?\\SWD#MMDEVAPI#{device-id}#"
        "{2eef81be-33fa-4800-9670-1cd474972c3f}",
        "Altitude capture endpoint ID normalized");

    TempConfigFile altitudeFile("[GENERAL]\nVALUE=keep\n");
    altitude_audio_guard::Config config{};
    config.enabled = true;
    config.inputDeviceId = "{capture-id}";
    config.outputDeviceId = "{render-id}";
    auto result = altitude_audio_guard::ensureConfig(
        altitudeFile.path, config, false, {});
    expect(result.status == altitude_audio_guard::Status::Updated,
        "audio guard writes an explicitly configured endpoint pair");
    std::ifstream input(altitudeFile.path);
    const std::string written{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    expect(written.find("[GENERAL]\nVALUE=keep") != std::string::npos &&
        written.find("[AUDIO]\nINPUT=\\\\?\\SWD#MMDEVAPI#{capture-id}") !=
            std::string::npos &&
        written.find("OUTPUT=\\\\?\\SWD#MMDEVAPI#{render-id}") !=
            std::string::npos,
        "audio guard persists endpoints without losing unrelated settings");
    result = altitude_audio_guard::ensureConfig(altitudeFile.path, config, false, {});
    expect(result.status == altitude_audio_guard::Status::Unchanged,
        "audio guard filesystem update is idempotent");
}

void testSuccessChimePolicy() {
    expect(auto_unicom::shouldPlaySuccessChime(auto_unicom::ResultCode::SubmittedVisible),
        "visible submission permits chime");
    expect(!auto_unicom::shouldPlaySuccessChime(
        auto_unicom::ResultCode::UncertainAfterSubmit),
        "uncertain submission never confirms with chime");
}

} // namespace

int main() {
    testEffectiveCallsign();
    testTextValidation();
    testGate();
    testMailbox();
    testVoiceValidationAndReceiveGuard();
    testComposerRetry();
    testComposerOwnershipRecoveryPolicy();
    testConfig();
    testAltitudeAudioGuard();
    testSuccessChimePolicy();
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All Auto-Unicom helper tests passed\n";
    return 0;
}
