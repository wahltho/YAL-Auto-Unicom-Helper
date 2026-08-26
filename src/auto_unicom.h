#pragma once

#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace auto_unicom {

constexpr int kApiVersion = 3;
constexpr std::size_t kMaxMessageBytes = 254;
constexpr std::size_t kMaxRequestBytes = 4096;

std::string selectEffectiveCallsign(
    std::string_view configuredCallsign,
    std::string_view activeFlightPlanCallsign
);

enum class Mode : int {
    Off = 0,
    DryRun = 1,
    Send = 2,
};

enum class TransportState : int {
    Unavailable = 0,
    Offline = 1,
    NotUnicom = 2,
    BlockedAtc = 3,
    Busy = 4,
    Ready = 5,
    Error = 6,
};

enum class ResultCode : int {
    Idle = 0,
    Accepted = 10,
    PreviewReady = 20,
    SubmittedVisible = 21,
    RejectedText = 30,
    RejectedPolicy = 31,
    RejectedContext = 32,
    FailedBeforeSubmit = 40,
    UncertainAfterSubmit = 41,
    Cancelled = 42,
};

struct TextValidation {
    bool valid = false;
    std::string normalized;
    std::string detail;
};

TextValidation validateMessageText(std::string_view text, bool inputOverflow = false);

struct GateSnapshot {
    Mode mode = Mode::Off;
    bool initialized = false;
    bool platformSupported = false;
    bool pilotUiAvailable = false;
    bool onlineKnown = false;
    bool online = false;
    bool frequencyKnown = false;
    int activeCom = 0;
    int activeFrequencyKhz = 0;
    int unicomFrequencyKhz = 122800;
    bool busy = false;
    long long ageMs = 0;
    long long maxAgeMs = 2500;
};

struct GateDecision {
    bool allowed = false;
    TransportState state = TransportState::Unavailable;
    ResultCode rejectionCode = ResultCode::FailedBeforeSubmit;
    std::string detail;
};

GateDecision evaluateGate(const GateSnapshot& snapshot, bool includeBusy = true);

struct Request {
    int sequence = 0;
    std::string text;
    bool inputOverflow = false;
    int channels = 1;
    std::string voiceText;
    bool voiceInputOverflow = false;
};

enum class CommitResult {
    Accepted,
    DuplicateOrOld,
    Busy,
    InvalidSequence,
};

class Mailbox {
public:
    int writeRequestText(const void* value, int offset, int byteCount);
    std::string requestText() const;
    int writeRequestVoiceText(const void* value, int offset, int byteCount);
    std::string requestVoiceText() const;
    void setRequestChannels(int channels);
    int requestChannels() const;

    CommitResult commit(int sequence);
    std::optional<Request> takePending();
    void publish(int sequence, ResultCode code, std::string detail);
    void cancelActive(std::string detail);

    bool busy() const;
    int requestSequence() const;
    int resultSequence() const;
    ResultCode resultCode() const;
    std::string resultDetail() const;

private:
    mutable std::mutex mutex_;
    std::string requestText_;
    bool requestOverflow_ = false;
    std::string requestVoiceText_;
    bool requestVoiceOverflow_ = false;
    int requestChannels_ = 1;
    int lastCommittedSequence_ = 0;
    int activeSequence_ = 0;
    std::optional<Request> pending_;
    int resultSequence_ = 0;
    ResultCode resultCode_ = ResultCode::Idle;
    std::string resultDetail_ = "IDLE";
};

const char* resultCodeName(ResultCode code);
const char* transportStateName(TransportState state);
bool shouldPlaySuccessChime(ResultCode code);

} // namespace auto_unicom
