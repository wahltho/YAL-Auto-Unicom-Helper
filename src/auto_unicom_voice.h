#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace auto_unicom_voice {

constexpr int kChannelText = 1;
constexpr int kChannelVoice = 2;
constexpr int kMaxVoiceTextBytes = 1024;
inline constexpr char kCom1ReceiveDataRef[] = "ivaopilot/audio/com1_rx";
inline constexpr char kCom2ReceiveDataRef[] = "ivaopilot/audio/com2_rx";

enum class DeliveryMode {
    Off,
    LocalReadback,
    Radio,
};

DeliveryMode parseDeliveryMode(std::string_view value, DeliveryMode fallback);
const char* deliveryModeName(DeliveryMode mode);

enum class State : int {
    Disabled = 0,
    Ready = 1,
    Preparing = 2,
    WaitingGate = 3,
    WaitingReceive = 4,
    Keying = 5,
    Transmitting = 6,
    Error = 7,
};

enum class ResultCode : int {
    Idle = 0,
    NotRequested = 1,
    Accepted = 10,
    Transmitted = 20,
    RejectedText = 30,
    Disabled = 31,
    RejectedContext = 32,
    FailedBeforePtt = 40,
    UncertainAfterPtt = 41,
    Cancelled = 42,
};

struct RequestValidation {
    bool valid = false;
    bool voiceRequested = false;
    std::string normalizedVoiceText;
    std::string detail;
};

RequestValidation validateRequest(
    int channels,
    std::string_view voiceText,
    bool inputOverflow = false
);

class ReceiveQuietTracker {
public:
    explicit ReceiveQuietTracker(int quietMs);
    bool observe(bool receiveActive, std::int64_t nowMs);

private:
    int quietMs_ = 0;
    std::int64_t idleSinceMs_ = -1;
};

struct Config {
    bool enabled = false;
    bool allowDefaultOutput = false;
    std::string outputDeviceId;
    std::string outputDeviceMatch;
    std::string sapiVoiceName;
    int sapiRate = 0;
    int volume = 100;
    int pttLeadMs = 250;
    int pttTailMs = 250;
    int receiveWaitMs = 30000;
    int receiveQuietMs = 1000;
    int pttConfirmMs = 2500;
};

struct Callbacks {
    std::function<void(const std::string&)> log;
    std::function<bool()> shouldStop;
    std::function<bool()> contextAllowed;
    std::function<bool(std::string&)> finalGate;
    std::function<bool()> receiveStatusKnown;
    std::function<bool()> receiveActive;
    std::function<bool()> pttStatusKnown;
    std::function<bool()> pttActive;
    std::function<bool()> pttDown;
    std::function<void()> pttUp;
    std::function<void(State)> stateChanged;
};

struct Result {
    ResultCode code = ResultCode::Idle;
    std::string detail = "IDLE";
};

Result transmit(const Config& config, const std::string& text, const Callbacks& callbacks);
Result playAudioTest(const Config& config, const std::string& text, const Callbacks& callbacks);

const char* resultCodeName(ResultCode code);
const char* stateName(State state);

} // namespace auto_unicom_voice
