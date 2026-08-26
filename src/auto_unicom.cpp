#include "auto_unicom.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace auto_unicom {

namespace {

bool isCollapsibleWhitespace(unsigned char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

std::string normalizeCallsign(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }

    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return normalized;
}

GateDecision reject(
    TransportState state,
    ResultCode code,
    std::string detail
) {
    return {false, state, code, std::move(detail)};
}

} // namespace

std::string selectEffectiveCallsign(
    std::string_view configuredCallsign,
    std::string_view activeFlightPlanCallsign
) {
    std::string configured = normalizeCallsign(configuredCallsign);
    if (!configured.empty()) {
        return configured;
    }
    return normalizeCallsign(activeFlightPlanCallsign);
}

TextValidation validateMessageText(std::string_view text, bool inputOverflow) {
    if (inputOverflow || text.size() > kMaxRequestBytes) {
        return {false, {}, "TEXT_INPUT_TOO_LONG"};
    }

    // SASL string properties append one terminator; any remaining NUL fails below.
    if (!text.empty() && text.back() == '\0') {
        text.remove_suffix(1);
    }

    std::string normalized;
    normalized.reserve(text.size());
    bool pendingSpace = false;
    for (unsigned char value : text) {
        if (isCollapsibleWhitespace(value)) {
            pendingSpace = !normalized.empty();
            continue;
        }
        if (value < 0x20 || value > 0x7e) {
            return {false, {}, "TEXT_NOT_PRINTABLE_ASCII"};
        }
        if (pendingSpace) {
            normalized.push_back(' ');
            pendingSpace = false;
        }
        normalized.push_back(static_cast<char>(value));
    }

    if (normalized.empty()) {
        return {false, {}, "TEXT_EMPTY"};
    }
    if (normalized.front() == '.') {
        return {false, {}, "TEXT_COMMAND_FORBIDDEN"};
    }
    if (normalized.size() > kMaxMessageBytes) {
        return {false, {}, "TEXT_TOO_LONG"};
    }
    return {true, std::move(normalized), "TEXT_OK"};
}

GateDecision evaluateGate(const GateSnapshot& snapshot, bool includeBusy) {
    if (snapshot.mode == Mode::Off) {
        return reject(TransportState::Unavailable, ResultCode::RejectedPolicy, "MODE_OFF");
    }
    if (!snapshot.initialized) {
        return reject(TransportState::Unavailable, ResultCode::FailedBeforeSubmit, "MONITOR_UNAVAILABLE");
    }
    if (snapshot.mode == Mode::Send && !snapshot.platformSupported) {
        return reject(TransportState::Unavailable, ResultCode::FailedBeforeSubmit, "PLATFORM_UNSUPPORTED");
    }
    if (snapshot.mode == Mode::Send && !snapshot.pilotUiAvailable) {
        return reject(TransportState::Unavailable, ResultCode::FailedBeforeSubmit, "PILOTUI_UNAVAILABLE");
    }
    if (snapshot.ageMs < 0 || snapshot.ageMs > snapshot.maxAgeMs) {
        return reject(TransportState::Error, ResultCode::FailedBeforeSubmit, "GATE_STALE");
    }
    if (!snapshot.onlineKnown) {
        return reject(TransportState::Error, ResultCode::FailedBeforeSubmit, "ONLINE_UNKNOWN");
    }
    if (!snapshot.online) {
        return reject(TransportState::Offline, ResultCode::RejectedContext, "OFFLINE");
    }
    if (!snapshot.frequencyKnown) {
        return reject(TransportState::Error, ResultCode::FailedBeforeSubmit, "TX_FREQUENCY_UNKNOWN");
    }
    if (snapshot.activeFrequencyKhz != snapshot.unicomFrequencyKhz) {
        return reject(TransportState::NotUnicom, ResultCode::RejectedContext, "NOT_UNICOM");
    }
    if (includeBusy && snapshot.busy) {
        return reject(TransportState::Busy, ResultCode::RejectedContext, "BUSY");
    }
    return {true, TransportState::Ready, ResultCode::Idle, "READY"};
}

int Mailbox::writeRequestText(const void* value, int offset, int byteCount) {
    if (offset < 0 || byteCount < 0 || (byteCount > 0 && !value)) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (offset == 0) {
        requestText_.clear();
        requestOverflow_ = false;
    }

    const std::size_t start = static_cast<std::size_t>(offset);
    const std::size_t count = static_cast<std::size_t>(byteCount);
    if (start > kMaxRequestBytes || count > kMaxRequestBytes - std::min(start, kMaxRequestBytes)) {
        requestOverflow_ = true;
    }
    if (start >= kMaxRequestBytes || count == 0) {
        return byteCount;
    }

    const std::size_t writable = std::min(count, kMaxRequestBytes - start);
    if (requestText_.size() < start) {
        requestText_.resize(start, '\0');
    }
    if (requestText_.size() < start + writable) {
        requestText_.resize(start + writable);
    }
    std::memcpy(requestText_.data() + start, value, writable);
    return byteCount;
}

std::string Mailbox::requestText() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return requestText_;
}

int Mailbox::writeRequestVoiceText(const void* value, int offset, int byteCount) {
    if (offset < 0 || byteCount < 0 || (byteCount > 0 && !value)) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (offset == 0) {
        requestVoiceText_.clear();
        requestVoiceOverflow_ = false;
    }
    const std::size_t start = static_cast<std::size_t>(offset);
    const std::size_t count = static_cast<std::size_t>(byteCount);
    if (start > kMaxRequestBytes || count > kMaxRequestBytes - std::min(start, kMaxRequestBytes)) {
        requestVoiceOverflow_ = true;
    }
    if (start >= kMaxRequestBytes || count == 0) {
        return byteCount;
    }
    const std::size_t writable = std::min(count, kMaxRequestBytes - start);
    if (requestVoiceText_.size() < start) {
        requestVoiceText_.resize(start, '\0');
    }
    if (requestVoiceText_.size() < start + writable) {
        requestVoiceText_.resize(start + writable);
    }
    std::memcpy(requestVoiceText_.data() + start, value, writable);
    return byteCount;
}

std::string Mailbox::requestVoiceText() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return requestVoiceText_;
}

void Mailbox::setRequestChannels(int channels) {
    std::lock_guard<std::mutex> lock(mutex_);
    requestChannels_ = channels;
}

int Mailbox::requestChannels() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return requestChannels_;
}

CommitResult Mailbox::commit(int sequence) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (sequence <= 0) {
        return CommitResult::InvalidSequence;
    }
    if (sequence <= lastCommittedSequence_) {
        return CommitResult::DuplicateOrOld;
    }
    if (activeSequence_ != 0 || pending_) {
        return CommitResult::Busy;
    }

    lastCommittedSequence_ = sequence;
    activeSequence_ = sequence;
    pending_ = Request{
        sequence,
        requestText_,
        requestOverflow_,
        requestChannels_,
        requestVoiceText_,
        requestVoiceOverflow_,
    };
    requestChannels_ = 1;
    requestVoiceText_.clear();
    requestVoiceOverflow_ = false;
    resultSequence_ = sequence;
    resultCode_ = ResultCode::Accepted;
    resultDetail_ = "ACCEPTED";
    return CommitResult::Accepted;
}

std::optional<Request> Mailbox::takePending() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pending_) {
        return std::nullopt;
    }
    auto request = std::move(pending_);
    pending_.reset();
    return request;
}

void Mailbox::publish(int sequence, ResultCode code, std::string detail) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (sequence <= 0 || sequence != activeSequence_) {
        return;
    }
    resultSequence_ = sequence;
    resultCode_ = code;
    resultDetail_ = detail.empty() ? resultCodeName(code) : std::move(detail);
    if (code != ResultCode::Accepted) {
        activeSequence_ = 0;
        pending_.reset();
    }
}

void Mailbox::cancelActive(std::string detail) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (activeSequence_ == 0) {
        return;
    }
    resultSequence_ = activeSequence_;
    resultCode_ = ResultCode::Cancelled;
    resultDetail_ = detail.empty() ? "CANCELLED" : std::move(detail);
    activeSequence_ = 0;
    pending_.reset();
}

bool Mailbox::busy() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return activeSequence_ != 0 || pending_.has_value();
}

int Mailbox::requestSequence() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastCommittedSequence_;
}

int Mailbox::resultSequence() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return resultSequence_;
}

ResultCode Mailbox::resultCode() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return resultCode_;
}

std::string Mailbox::resultDetail() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return resultDetail_;
}

const char* resultCodeName(ResultCode code) {
    switch (code) {
    case ResultCode::Accepted:
        return "ACCEPTED";
    case ResultCode::PreviewReady:
        return "PREVIEW_READY";
    case ResultCode::SubmittedVisible:
        return "SUBMITTED_VISIBLE";
    case ResultCode::RejectedText:
        return "REJECTED_TEXT";
    case ResultCode::RejectedPolicy:
        return "REJECTED_POLICY";
    case ResultCode::RejectedContext:
        return "REJECTED_CONTEXT";
    case ResultCode::FailedBeforeSubmit:
        return "FAILED_BEFORE_SUBMIT";
    case ResultCode::UncertainAfterSubmit:
        return "UNCERTAIN_AFTER_SUBMIT";
    case ResultCode::Cancelled:
        return "CANCELLED";
    case ResultCode::Idle:
    default:
        return "IDLE";
    }
}

const char* transportStateName(TransportState state) {
    switch (state) {
    case TransportState::Offline:
        return "OFFLINE";
    case TransportState::NotUnicom:
        return "NOT_UNICOM";
    case TransportState::BlockedAtc:
        return "BLOCKED_ATC";
    case TransportState::Busy:
        return "BUSY";
    case TransportState::Ready:
        return "READY";
    case TransportState::Error:
        return "ERROR";
    case TransportState::Unavailable:
    default:
        return "UNAVAILABLE";
    }
}

bool shouldPlaySuccessChime(ResultCode code) {
    return code == ResultCode::SubmittedVisible;
}

} // namespace auto_unicom
