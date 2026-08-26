#pragma once

#include <functional>
#include <string>

namespace pilotui_message {

constexpr int kComposerAmbiguousRetryDelayMs = 500;

enum class Status {
    SubmittedVisible,
    RejectedByGate,
    FailedBeforeSubmit,
    UncertainAfterSubmit,
    Cancelled,
    Unsupported,
};

struct Options {
    std::string windowTitle;
    std::string composerName = "Message";
    std::string sendButtonName = "SEND";
    std::string message;
    int confirmTimeoutMs = 5000;
    int pollMs = 100;
    bool debug = false;
};

struct Callbacks {
    std::function<void(const std::string&)> log;
    std::function<bool(std::string&)> finalGate;
    std::function<bool()> shouldStop;
};

struct Result {
    Status status = Status::FailedBeforeSubmit;
    std::string detail;
    int baselineMatches = 0;
    int finalMatches = 0;
};

using SubmitAttempt = std::function<Result()>;
using RetryWait = std::function<bool(int delayMs)>;

Result submitWithComposerAmbiguityRetry(
    const SubmitAttempt& submitAttempt,
    const RetryWait& retryWait,
    const Callbacks& callbacks
);

struct DiscoveryResult {
    bool success = false;
    std::string detail;
    int historySurfaces = 0;
    bool composerEmpty = false;
};

DiscoveryResult discoverActiveFrequencyControls(const Options& options, const Callbacks& callbacks);
Result submitActiveFrequencyMessage(const Options& options, const Callbacks& callbacks);

} // namespace pilotui_message
