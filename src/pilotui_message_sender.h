#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace pilotui_message {

constexpr int kComposerAmbiguousRetryDelayMs = 500;

enum class ComposerOwnershipMatch {
    Empty,
    Exact,
    Prefix,
    Foreign,
};

class ComposerOwnershipTracker {
public:
    void beginWrite(std::string expectedText, std::int64_t startedAtMs);
    void markComposed();
    void clear();
    bool active() const;
    bool recoveryDue(std::int64_t nowMs, int staleMs) const;
    ComposerOwnershipMatch classify(const std::string& currentText) const;

private:
    std::string expectedText_;
    std::int64_t startedAtMs_ = 0;
    bool active_ = false;
    bool composed_ = false;
};

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

enum class RecoveryStatus {
    None,
    Cleared,
    ComposerEmpty,
    ForeignPreserved,
    Deferred,
    Failed,
    Unsupported,
};

struct RecoveryResult {
    RecoveryStatus status = RecoveryStatus::None;
    std::string detail;
};

DiscoveryResult discoverActiveFrequencyControls(const Options& options, const Callbacks& callbacks);
Result submitActiveFrequencyMessage(const Options& options, const Callbacks& callbacks);
bool hasOwnedComposerDraft();
bool ownedComposerRecoveryDue(int staleMs);
RecoveryResult recoverOwnedComposerDraft(const Options& options, const Callbacks& callbacks);

} // namespace pilotui_message
