#include "auto_unicom_voice.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cwctype>
#include <cstring>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#if IBM
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <sapi.h>
#include <sphelper.h>
#endif

namespace auto_unicom_voice {

namespace {

bool isWhitespace(unsigned char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

void setState(const Callbacks& callbacks, State state) {
    if (callbacks.stateChanged) {
        callbacks.stateChanged(state);
    }
}

#if IBM

void logLine(const Callbacks& callbacks, const std::string& line) {
    if (callbacks.log) {
        callbacks.log(line);
    }
}

bool shouldStop(const Callbacks& callbacks) {
    return callbacks.shouldStop && callbacks.shouldStop();
}

bool contextAllowed(const Callbacks& callbacks) {
    return !callbacks.contextAllowed || callbacks.contextAllowed();
}

bool waitInterruptible(int milliseconds, const Callbacks& callbacks, bool monitorContext = false) {
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(std::max(0, milliseconds));
    while (std::chrono::steady_clock::now() < deadline) {
        if (shouldStop(callbacks) || (monitorContext && !contextAllowed(callbacks))) {
            return false;
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()
        ).count();
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min<long long>(10, remaining)));
    }
    return !shouldStop(callbacks) && (!monitorContext || contextAllowed(callbacks));
}

template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& other) noexcept : value_(other.value_) { other.value_ = nullptr; }
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            reset();
            value_ = other.value_;
            other.value_ = nullptr;
        }
        return *this;
    }
    T* get() const { return value_; }
    T** put() {
        reset();
        return &value_;
    }
    T* operator->() const { return value_; }
    explicit operator bool() const { return value_ != nullptr; }
    void reset() {
        if (value_) {
            value_->Release();
            value_ = nullptr;
        }
    }

private:
    T* value_ = nullptr;
};

class ComApartment {
public:
    ComApartment() {
        result_ = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        owns_ = result_ == S_OK || result_ == S_FALSE;
    }
    ~ComApartment() {
        if (owns_) {
            CoUninitialize();
        }
    }
    bool ready() const { return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE; }
    HRESULT result() const { return result_; }

private:
    HRESULT result_ = E_FAIL;
    bool owns_ = false;
};

std::string hresultDetail(const char* prefix, HRESULT result) {
    std::ostringstream out;
    out << prefix << ":0x" << std::hex << std::uppercase
        << static_cast<unsigned long>(result);
    return out.str();
}

std::wstring widenUtf8(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) {
        size = MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (size <= 0) {
            return {};
        }
        std::wstring wide(static_cast<std::size_t>(size), L'\0');
        MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
        return wide;
    }
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

std::string narrowUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string narrow(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        narrow.data(), size, nullptr, nullptr);
    return narrow;
}

std::wstring lowerWide(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t value) {
        return static_cast<wchar_t>(std::towlower(value));
    });
    return text;
}

struct SynthesizedAudio {
    WAVEFORMATEX format{};
    std::vector<unsigned char> pcm;
};

const GUID kWaveFormatExId = {
    0xc31adbae, 0x527f, 0x4ff5, {0xa2, 0x30, 0xf6, 0x2b, 0xb6, 0x1f, 0xf7, 0x0c}
};

HRESULT selectSapiVoice(ISpVoice* voice, const std::string& requestedName) {
    if (!voice || requestedName.empty()) {
        return S_OK;
    }
    const std::wstring needle = lowerWide(widenUtf8(requestedName));
    ComPtr<IEnumSpObjectTokens> tokens;
    HRESULT result = SpEnumTokens(SPCAT_VOICES, nullptr, nullptr, tokens.put());
    if (FAILED(result)) {
        return result;
    }
    ULONG count = 0;
    result = tokens->GetCount(&count);
    if (FAILED(result)) {
        return result;
    }
    for (ULONG index = 0; index < count; ++index) {
        ComPtr<ISpObjectToken> token;
        result = tokens->Item(index, token.put());
        if (FAILED(result)) {
            continue;
        }
        WCHAR* description = nullptr;
        result = SpGetDescription(token.get(), &description);
        std::wstring candidate = description ? description : L"";
        if (description) {
            CoTaskMemFree(description);
        }
        if (SUCCEEDED(result) && lowerWide(candidate).find(needle) != std::wstring::npos) {
            return voice->SetVoice(token.get());
        }
    }
    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

bool synthesizeSapi(
    const Config& config,
    const std::string& text,
    SynthesizedAudio& audio,
    std::string& detail
) {
    ComPtr<ISpVoice> voice;
    HRESULT result = CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_INPROC_SERVER,
        IID_ISpVoice, reinterpret_cast<void**>(voice.put()));
    if (FAILED(result)) {
        detail = hresultDetail("SAPI_CREATE_FAILED", result);
        return false;
    }
    result = selectSapiVoice(voice.get(), config.sapiVoiceName);
    if (FAILED(result)) {
        detail = hresultDetail("SAPI_VOICE_NOT_FOUND", result);
        return false;
    }
    result = voice->SetRate(config.sapiRate);
    if (FAILED(result)) {
        detail = hresultDetail("SAPI_RATE_FAILED", result);
        return false;
    }
    result = voice->SetVolume(static_cast<USHORT>(config.volume));
    if (FAILED(result)) {
        detail = hresultDetail("SAPI_VOLUME_FAILED", result);
        return false;
    }

    ComPtr<IStream> memory;
    result = CreateStreamOnHGlobal(nullptr, TRUE, memory.put());
    if (FAILED(result)) {
        detail = hresultDetail("SAPI_MEMORY_FAILED", result);
        return false;
    }
    ComPtr<ISpStream> stream;
    result = CoCreateInstance(CLSID_SpStream, nullptr, CLSCTX_INPROC_SERVER,
        IID_ISpStream, reinterpret_cast<void**>(stream.put()));
    if (FAILED(result)) {
        detail = hresultDetail("SAPI_STREAM_FAILED", result);
        return false;
    }

    audio.format.wFormatTag = WAVE_FORMAT_PCM;
    audio.format.nChannels = 1;
    audio.format.nSamplesPerSec = 24000;
    audio.format.wBitsPerSample = 16;
    audio.format.nBlockAlign = static_cast<WORD>(
        audio.format.nChannels * audio.format.wBitsPerSample / 8);
    audio.format.nAvgBytesPerSec = audio.format.nSamplesPerSec * audio.format.nBlockAlign;
    audio.format.cbSize = 0;
    result = stream->SetBaseStream(memory.get(), kWaveFormatExId, &audio.format);
    if (FAILED(result)) {
        detail = hresultDetail("SAPI_FORMAT_FAILED", result);
        return false;
    }
    result = voice->SetOutput(stream.get(), TRUE);
    if (FAILED(result)) {
        detail = hresultDetail("SAPI_OUTPUT_FAILED", result);
        return false;
    }
    const std::wstring wideText = widenUtf8(text);
    if (wideText.empty()) {
        detail = "SAPI_TEXT_CONVERSION_FAILED";
        return false;
    }
    result = voice->Speak(wideText.c_str(), SPF_DEFAULT, nullptr);
    if (FAILED(result)) {
        detail = hresultDetail("SAPI_SPEAK_FAILED", result);
        return false;
    }
    voice->SetOutput(nullptr, FALSE);

    STATSTG stat{};
    result = memory->Stat(&stat, STATFLAG_NONAME);
    if (FAILED(result) || stat.cbSize.QuadPart <= 0 ||
        stat.cbSize.QuadPart > static_cast<LONGLONG>(64 * 1024 * 1024)) {
        detail = FAILED(result) ? hresultDetail("SAPI_SIZE_FAILED", result) : "SAPI_AUDIO_EMPTY";
        return false;
    }
    LARGE_INTEGER start{};
    result = memory->Seek(start, STREAM_SEEK_SET, nullptr);
    if (FAILED(result)) {
        detail = hresultDetail("SAPI_SEEK_FAILED", result);
        return false;
    }
    audio.pcm.resize(static_cast<std::size_t>(stat.cbSize.QuadPart));
    ULONG read = 0;
    result = memory->Read(audio.pcm.data(), static_cast<ULONG>(audio.pcm.size()), &read);
    if (FAILED(result) || read != audio.pcm.size()) {
        detail = FAILED(result) ? hresultDetail("SAPI_READ_FAILED", result) : "SAPI_READ_SHORT";
        return false;
    }
    detail = "SAPI_READY";
    return true;
}

bool deviceFriendlyName(IMMDevice* device, std::wstring& name) {
    ComPtr<IPropertyStore> properties;
    HRESULT result = device->OpenPropertyStore(STGM_READ, properties.put());
    if (FAILED(result)) {
        return false;
    }
    PROPVARIANT value;
    PropVariantInit(&value);
    result = properties->GetValue(PKEY_Device_FriendlyName, &value);
    if (SUCCEEDED(result) && value.vt == VT_LPWSTR && value.pwszVal) {
        name = value.pwszVal;
    }
    PropVariantClear(&value);
    return SUCCEEDED(result) && !name.empty();
}

bool selectRenderDevice(
    const Config& config,
    IMMDeviceEnumerator* enumerator,
    ComPtr<IMMDevice>& device,
    std::string& selectedName,
    std::string& detail
) {
    if (!config.outputDeviceId.empty()) {
        const std::wstring id = widenUtf8(config.outputDeviceId);
        HRESULT result = enumerator->GetDevice(id.c_str(), device.put());
        if (SUCCEEDED(result)) {
            std::wstring friendly;
            deviceFriendlyName(device.get(), friendly);
            selectedName = narrowUtf8(friendly);
            return true;
        }
        detail = hresultDetail("VOICE_OUTPUT_ID_NOT_FOUND", result);
        return false;
    }
    if (config.outputDeviceMatch.empty()) {
        if (!config.allowDefaultOutput) {
            detail = "VOICE_OUTPUT_NOT_CONFIGURED";
            return false;
        }
        HRESULT result = enumerator->GetDefaultAudioEndpoint(
            eRender,
            eMultimedia,
            device.put()
        );
        if (FAILED(result)) {
            detail = hresultDetail("VOICE_DEFAULT_OUTPUT_FAILED", result);
            return false;
        }
        std::wstring friendly;
        deviceFriendlyName(device.get(), friendly);
        selectedName = narrowUtf8(friendly);
        return true;
    }

    const std::wstring needle = lowerWide(widenUtf8(config.outputDeviceMatch));
    ComPtr<IMMDeviceCollection> devices;
    HRESULT result = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, devices.put());
    if (FAILED(result)) {
        detail = hresultDetail("VOICE_OUTPUT_ENUM_FAILED", result);
        return false;
    }
    UINT count = 0;
    devices->GetCount(&count);
    for (UINT index = 0; index < count; ++index) {
        ComPtr<IMMDevice> candidate;
        if (FAILED(devices->Item(index, candidate.put()))) {
            continue;
        }
        std::wstring friendly;
        if (!deviceFriendlyName(candidate.get(), friendly)) {
            continue;
        }
        if (lowerWide(friendly).find(needle) != std::wstring::npos) {
            selectedName = narrowUtf8(friendly);
            device = std::move(candidate);
            return true;
        }
    }
    detail = "VOICE_OUTPUT_MATCH_NOT_FOUND";
    return false;
}

class WasapiRenderer {
public:
    bool open(const Config& config, const WAVEFORMATEX& format, std::string& detail) {
        ComPtr<IMMDeviceEnumerator> enumerator;
        HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator.put()));
        if (FAILED(result)) {
            detail = hresultDetail("WASAPI_ENUMERATOR_FAILED", result);
            return false;
        }
        if (!selectRenderDevice(config, enumerator.get(), device_, selectedName_, detail)) {
            return false;
        }
        result = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(client_.put()));
        if (FAILED(result)) {
            detail = hresultDetail("WASAPI_CLIENT_FAILED", result);
            return false;
        }
        WAVEFORMATEX requested = format;
        const DWORD flags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
            AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY | AUDCLNT_STREAMFLAGS_NOPERSIST;
        result = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 1000000, 0, &requested, nullptr);
        if (FAILED(result)) {
            detail = hresultDetail("WASAPI_INITIALIZE_FAILED", result);
            return false;
        }
        result = client_->GetBufferSize(&bufferFrames_);
        if (FAILED(result)) {
            detail = hresultDetail("WASAPI_BUFFER_SIZE_FAILED", result);
            return false;
        }
        result = client_->GetService(__uuidof(IAudioRenderClient),
            reinterpret_cast<void**>(render_.put()));
        if (FAILED(result)) {
            detail = hresultDetail("WASAPI_RENDER_SERVICE_FAILED", result);
            return false;
        }
        blockAlign_ = requested.nBlockAlign;
        sampleRate_ = requested.nSamplesPerSec;
        detail = "WASAPI_READY:" + selectedName_;
        return true;
    }

    bool play(const std::vector<unsigned char>& pcm, const Callbacks& callbacks, std::string& detail) {
        if (!client_ || !render_ || blockAlign_ == 0 || pcm.empty()) {
            detail = "WASAPI_NOT_READY";
            return false;
        }
        const UINT32 totalFrames = static_cast<UINT32>(pcm.size() / blockAlign_);
        if (totalFrames == 0) {
            detail = "WASAPI_AUDIO_EMPTY";
            return false;
        }
        HRESULT result = client_->Start();
        if (FAILED(result)) {
            detail = hresultDetail("WASAPI_START_FAILED", result);
            return false;
        }

        UINT32 writtenFrames = 0;
        bool ok = true;
        const auto playbackDeadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(
                static_cast<long long>(totalFrames) * 1000 /
                    std::max<DWORD>(1, sampleRate_) + 5000
            );
        while (writtenFrames < totalFrames) {
            if (shouldStop(callbacks)) {
                detail = "CANCELLED_DURING_AUDIO";
                ok = false;
                break;
            }
            if (!contextAllowed(callbacks)) {
                detail = "VOICE_CONTEXT_CHANGED_AFTER_PTT";
                ok = false;
                break;
            }
            if (std::chrono::steady_clock::now() >= playbackDeadline) {
                detail = "WASAPI_PLAYBACK_TIMEOUT";
                ok = false;
                break;
            }
            UINT32 padding = 0;
            result = client_->GetCurrentPadding(&padding);
            if (FAILED(result)) {
                detail = hresultDetail("WASAPI_PADDING_FAILED", result);
                ok = false;
                break;
            }
            const UINT32 available = bufferFrames_ > padding ? bufferFrames_ - padding : 0;
            if (available == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            const UINT32 count = std::min(available, totalFrames - writtenFrames);
            BYTE* destination = nullptr;
            result = render_->GetBuffer(count, &destination);
            if (FAILED(result)) {
                detail = hresultDetail("WASAPI_GET_BUFFER_FAILED", result);
                ok = false;
                break;
            }
            std::memcpy(destination, pcm.data() +
                static_cast<std::size_t>(writtenFrames) * blockAlign_,
                static_cast<std::size_t>(count) * blockAlign_);
            result = render_->ReleaseBuffer(count, 0);
            if (FAILED(result)) {
                detail = hresultDetail("WASAPI_RELEASE_BUFFER_FAILED", result);
                ok = false;
                break;
            }
            writtenFrames += count;
        }

        if (ok) {
            for (;;) {
                if (shouldStop(callbacks)) {
                    detail = "CANCELLED_DURING_AUDIO_DRAIN";
                    ok = false;
                    break;
                }
                if (!contextAllowed(callbacks)) {
                    detail = "VOICE_CONTEXT_CHANGED_AFTER_PTT";
                    ok = false;
                    break;
                }
                if (std::chrono::steady_clock::now() >= playbackDeadline) {
                    detail = "WASAPI_DRAIN_TIMEOUT";
                    ok = false;
                    break;
                }
                UINT32 padding = 0;
                result = client_->GetCurrentPadding(&padding);
                if (FAILED(result)) {
                    detail = hresultDetail("WASAPI_DRAIN_FAILED", result);
                    ok = false;
                    break;
                }
                if (padding == 0) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        client_->Stop();
        if (ok) {
            detail = "WASAPI_PLAYED:" + selectedName_;
        }
        return ok;
    }

private:
    ComPtr<IMMDevice> device_;
    ComPtr<IAudioClient> client_;
    ComPtr<IAudioRenderClient> render_;
    UINT32 bufferFrames_ = 0;
    WORD blockAlign_ = 0;
    DWORD sampleRate_ = 0;
    std::string selectedName_;
};

#endif

} // namespace

DeliveryMode parseDeliveryMode(std::string_view value, DeliveryMode fallback) {
    std::string normalized;
    normalized.reserve(value.size());
    for (unsigned char c : value) {
        if (c == ' ' || c == '-' || c == '_') {
            continue;
        }
        normalized.push_back(static_cast<char>(std::tolower(c)));
    }
    if (normalized == "off") {
        return DeliveryMode::Off;
    }
    if (normalized == "local" || normalized == "localreadback") {
        return DeliveryMode::LocalReadback;
    }
    if (normalized == "radio") {
        return DeliveryMode::Radio;
    }
    return fallback;
}

const char* deliveryModeName(DeliveryMode mode) {
    switch (mode) {
    case DeliveryMode::Off: return "off";
    case DeliveryMode::LocalReadback: return "local";
    case DeliveryMode::Radio: return "radio";
    }
    return "off";
}

RequestValidation validateRequest(int channels, std::string_view voiceText, bool inputOverflow) {
    if (channels != kChannelText && channels != (kChannelText | kChannelVoice)) {
        return {false, false, {}, "CHANNELS_INVALID"};
    }
    if ((channels & kChannelVoice) == 0) {
        return {true, false, {}, "VOICE_NOT_REQUESTED"};
    }
    if (inputOverflow || voiceText.size() > kMaxVoiceTextBytes) {
        return {false, true, {}, "VOICE_TEXT_TOO_LONG"};
    }
    if (!voiceText.empty() && voiceText.back() == '\0') {
        voiceText.remove_suffix(1);
    }

    std::string normalized;
    normalized.reserve(voiceText.size());
    bool pendingSpace = false;
    for (unsigned char value : voiceText) {
        if (isWhitespace(value)) {
            pendingSpace = !normalized.empty();
            continue;
        }
        if (value < 0x20 || value > 0x7e) {
            return {false, true, {}, "VOICE_TEXT_NOT_PRINTABLE_ASCII"};
        }
        if (pendingSpace) {
            normalized.push_back(' ');
            pendingSpace = false;
        }
        normalized.push_back(static_cast<char>(value));
    }
    if (normalized.empty()) {
        return {false, true, {}, "VOICE_TEXT_EMPTY"};
    }
    return {true, true, std::move(normalized), "VOICE_TEXT_OK"};
}

ReceiveQuietTracker::ReceiveQuietTracker(int quietMs)
    : quietMs_(std::max(0, quietMs)) {
}

bool ReceiveQuietTracker::observe(bool receiveActive, std::int64_t nowMs) {
    if (receiveActive) {
        idleSinceMs_ = -1;
        return false;
    }
    if (quietMs_ == 0) {
        return true;
    }
    if (idleSinceMs_ < 0 || nowMs < idleSinceMs_) {
        idleSinceMs_ = nowMs;
        return false;
    }
    return nowMs - idleSinceMs_ >= quietMs_;
}

Result transmit(const Config& config, const std::string& text, const Callbacks& callbacks) {
    if (!config.enabled) {
        setState(callbacks, State::Disabled);
        return {ResultCode::Disabled, "VOICE_DISABLED"};
    }
#if !IBM
    (void)text;
    setState(callbacks, State::Error);
    return {ResultCode::FailedBeforePtt, "VOICE_PLATFORM_UNSUPPORTED"};
#else
    setState(callbacks, State::Preparing);
    ComApartment apartment;
    if (!apartment.ready()) {
        setState(callbacks, State::Error);
        return {ResultCode::FailedBeforePtt, hresultDetail("COM_INIT_FAILED", apartment.result())};
    }
    SynthesizedAudio audio;
    WasapiRenderer renderer;
    std::string detail;
    if (!synthesizeSapi(config, text, audio, detail)) {
        setState(callbacks, State::Error);
        return {ResultCode::FailedBeforePtt, detail};
    }
    if (!renderer.open(config, audio.format, detail)) {
        setState(callbacks, State::Error);
        return {ResultCode::FailedBeforePtt, detail};
    }
    logLine(callbacks, "Auto UNICOM voice: prepared " + std::to_string(audio.pcm.size()) +
        " PCM bytes " + detail);
    if (shouldStop(callbacks)) {
        setState(callbacks, State::Ready);
        return {ResultCode::Cancelled, "CANCELLED_BEFORE_PTT"};
    }

    setState(callbacks, State::WaitingGate);
    if (!callbacks.finalGate) {
        setState(callbacks, State::Error);
        return {ResultCode::FailedBeforePtt, "VOICE_FINAL_GATE_MISSING"};
    }
    detail.clear();
    if (!callbacks.finalGate(detail)) {
        setState(callbacks, State::Ready);
        return {ResultCode::RejectedContext,
            detail.empty() ? "VOICE_FINAL_GATE_REJECTED" : "VOICE_FINAL_GATE:" + detail};
    }
    if (!callbacks.receiveStatusKnown || !callbacks.receiveActive ||
        !callbacks.receiveStatusKnown()) {
        setState(callbacks, State::Error);
        return {ResultCode::FailedBeforePtt, "VOICE_RX_STATUS_UNAVAILABLE"};
    }
    if (!callbacks.pttStatusKnown || !callbacks.pttStatusKnown()) {
        setState(callbacks, State::Error);
        return {ResultCode::FailedBeforePtt, "VOICE_PTT_STATUS_UNAVAILABLE"};
    }

    setState(callbacks, State::WaitingReceive);
    const auto receiveDeadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(config.receiveWaitMs);
    ReceiveQuietTracker receiveQuiet(config.receiveQuietMs);
    logLine(callbacks, "Auto UNICOM voice: waiting for RX quiet window " +
        std::to_string(config.receiveQuietMs) + " ms");
    while (true) {
        if (shouldStop(callbacks)) {
            setState(callbacks, State::Ready);
            return {ResultCode::Cancelled, "CANCELLED_BEFORE_PTT"};
        }
        if (!callbacks.receiveStatusKnown()) {
            setState(callbacks, State::Error);
            return {ResultCode::FailedBeforePtt, "VOICE_RX_STATUS_UNAVAILABLE"};
        }
        const bool receiveActive = callbacks.receiveActive();
        const auto now = std::chrono::steady_clock::now();
        const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        ).count();
        if (receiveQuiet.observe(receiveActive, nowMs)) {
            break;
        }
        if (now >= receiveDeadline) {
            setState(callbacks, State::Ready);
            return {ResultCode::RejectedContext,
                receiveActive ? "VOICE_RX_BUSY_TIMEOUT" : "VOICE_RX_QUIET_TIMEOUT"};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    detail.clear();
    if (!callbacks.finalGate(detail)) {
        setState(callbacks, State::Ready);
        return {ResultCode::RejectedContext,
            detail.empty() ? "VOICE_FINAL_GATE_REJECTED" : "VOICE_FINAL_GATE:" + detail};
    }
    if (!callbacks.receiveStatusKnown()) {
        setState(callbacks, State::Error);
        return {ResultCode::FailedBeforePtt, "VOICE_RX_STATUS_UNAVAILABLE"};
    }
    if (callbacks.receiveActive()) {
        setState(callbacks, State::Ready);
        return {ResultCode::RejectedContext, "VOICE_RX_BECAME_ACTIVE"};
    }
    if (callbacks.pttActive && callbacks.pttActive()) {
        setState(callbacks, State::Ready);
        return {ResultCode::RejectedContext, "VOICE_PTT_ALREADY_ACTIVE"};
    }

    setState(callbacks, State::Keying);
    if (!callbacks.pttDown || !callbacks.pttUp || !callbacks.pttDown()) {
        setState(callbacks, State::Error);
        return {ResultCode::FailedBeforePtt, "VOICE_PTT_DOWN_FAILED"};
    }
    struct PttRelease {
        const Callbacks& callbacks;
        ~PttRelease() { callbacks.pttUp(); }
    } pttRelease{callbacks};

    const auto confirmDeadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(config.pttConfirmMs);
    while (!callbacks.pttActive || !callbacks.pttActive()) {
        if (shouldStop(callbacks)) {
            setState(callbacks, State::Error);
            return {ResultCode::UncertainAfterPtt, "CANCELLED_AFTER_PTT"};
        }
        if (!contextAllowed(callbacks)) {
            setState(callbacks, State::Error);
            return {ResultCode::UncertainAfterPtt, "VOICE_CONTEXT_CHANGED_AFTER_PTT"};
        }
        if (std::chrono::steady_clock::now() >= confirmDeadline) {
            setState(callbacks, State::Error);
            return {ResultCode::UncertainAfterPtt, "VOICE_PTT_NOT_CONFIRMED"};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (!waitInterruptible(config.pttLeadMs, callbacks, true)) {
        setState(callbacks, State::Error);
        return {ResultCode::UncertainAfterPtt,
            contextAllowed(callbacks) ? "CANCELLED_AFTER_PTT" : "VOICE_CONTEXT_CHANGED_AFTER_PTT"};
    }

    setState(callbacks, State::Transmitting);
    if (!renderer.play(audio.pcm, callbacks, detail)) {
        setState(callbacks, State::Error);
        return {ResultCode::UncertainAfterPtt, detail};
    }
    if (!waitInterruptible(config.pttTailMs, callbacks, true)) {
        setState(callbacks, State::Error);
        return {ResultCode::UncertainAfterPtt,
            contextAllowed(callbacks) ? "CANCELLED_AFTER_PTT" : "VOICE_CONTEXT_CHANGED_AFTER_PTT"};
    }
    setState(callbacks, State::Ready);
    return {ResultCode::Transmitted, "VOICE_TRANSMITTED"};
#endif
}

Result playAudioTest(const Config& config, const std::string& text, const Callbacks& callbacks) {
#if !IBM
    (void)config;
    (void)text;
    setState(callbacks, State::Error);
    return {ResultCode::FailedBeforePtt, "VOICE_PLATFORM_UNSUPPORTED"};
#else
    setState(callbacks, State::Preparing);
    ComApartment apartment;
    if (!apartment.ready()) {
        setState(callbacks, State::Error);
        return {ResultCode::FailedBeforePtt, hresultDetail("COM_INIT_FAILED", apartment.result())};
    }
    SynthesizedAudio audio;
    WasapiRenderer renderer;
    std::string detail;
    if (!synthesizeSapi(config, text, audio, detail) || !renderer.open(config, audio.format, detail)) {
        setState(callbacks, State::Error);
        return {ResultCode::FailedBeforePtt, detail};
    }
    setState(callbacks, State::Transmitting);
    if (!renderer.play(audio.pcm, callbacks, detail)) {
        setState(callbacks, State::Error);
        return {ResultCode::FailedBeforePtt, detail};
    }
    setState(callbacks, config.enabled ? State::Ready : State::Disabled);
    return {ResultCode::Transmitted, "VOICE_AUDIO_TEST_PLAYED"};
#endif
}

const char* resultCodeName(ResultCode code) {
    switch (code) {
    case ResultCode::NotRequested: return "NOT_REQUESTED";
    case ResultCode::Accepted: return "ACCEPTED";
    case ResultCode::Transmitted: return "TRANSMITTED";
    case ResultCode::RejectedText: return "REJECTED_TEXT";
    case ResultCode::Disabled: return "DISABLED";
    case ResultCode::RejectedContext: return "REJECTED_CONTEXT";
    case ResultCode::FailedBeforePtt: return "FAILED_BEFORE_PTT";
    case ResultCode::UncertainAfterPtt: return "UNCERTAIN_AFTER_PTT";
    case ResultCode::Cancelled: return "CANCELLED";
    case ResultCode::Idle:
    default: return "IDLE";
    }
}

const char* stateName(State state) {
    switch (state) {
    case State::Ready: return "READY";
    case State::Preparing: return "PREPARING";
    case State::WaitingGate: return "WAITING_GATE";
    case State::WaitingReceive: return "WAITING_RECEIVE";
    case State::Keying: return "KEYING";
    case State::Transmitting: return "TRANSMITTING";
    case State::Error: return "ERROR";
    case State::Disabled:
    default: return "DISABLED";
    }
}

} // namespace auto_unicom_voice
