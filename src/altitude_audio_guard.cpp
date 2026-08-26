#include "altitude_audio_guard.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

#if IBM
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#endif

namespace altitude_audio_guard {

namespace {

constexpr const char* kDefaultInputDeviceId = "{2EEF81BE-33FA-4800-9670-1CD474972C3F}";
constexpr const char* kDefaultOutputDeviceId = "{E6327CAD-DCEC-4949-AE8A-991E976A79D2}";
constexpr const char* kInputDeviceInterfaceGuid = "{2eef81be-33fa-4800-9670-1cd474972c3f}";
constexpr const char* kOutputDeviceInterfaceGuid = "{e6327cad-dcec-4949-ae8a-991e976a79d2}";

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

void emit(const Log& log, bool enabled, const std::string& message) {
    if (enabled && log) {
        log("Altitude audio guard: " + message);
    }
}

bool isLegacyDefaultId(const std::string& id) {
    const std::string normalized = upperAscii(trim(id));
    return normalized == kDefaultInputDeviceId || normalized == kDefaultOutputDeviceId;
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

bool updateLine(
    std::vector<std::string>& lines,
    std::size_t index,
    const std::string& key,
    const std::string& desired
) {
    if (desired.empty()) {
        return false;
    }
    const auto separator = lines[index].find('=');
    const std::string current = separator == std::string::npos
        ? std::string()
        : trim(lines[index].substr(separator + 1));
    if (current == desired) {
        return false;
    }
    lines[index] = key + "=" + desired;
    return true;
}

#if IBM
std::wstring widen(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), size);
    result.pop_back();
    return result;
}

std::string narrow(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), -1, result.data(), size, nullptr, nullptr);
    result.pop_back();
    return result;
}

std::wstring lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

std::string hresultHex(HRESULT value) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << static_cast<unsigned long>(value);
    return stream.str();
}

PROPERTYKEY friendlyNamePropertyKey() {
    PROPERTYKEY key{};
    key.fmtid.Data1 = 0xa45c254e;
    key.fmtid.Data2 = 0xdf1c;
    key.fmtid.Data3 = 0x4efd;
    key.fmtid.Data4[0] = 0x80;
    key.fmtid.Data4[1] = 0x20;
    key.fmtid.Data4[2] = 0x67;
    key.fmtid.Data4[3] = 0xd1;
    key.fmtid.Data4[4] = 0x46;
    key.fmtid.Data4[5] = 0xa8;
    key.fmtid.Data4[6] = 0x50;
    key.fmtid.Data4[7] = 0xe0;
    key.pid = 14;
    return key;
}

bool resolveDeviceByName(
    bool input,
    const std::string& rawMatch,
    std::string& id,
    std::string& name,
    std::string& error
) {
    id.clear();
    name.clear();
    error.clear();
    const std::string match = trim(rawMatch);
    if (match.empty()) {
        error = "empty match";
        return false;
    }

    const HRESULT initResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize = initResult == S_OK || initResult == S_FALSE;
    if (FAILED(initResult) && initResult != RPC_E_CHANGED_MODE) {
        error = "CoInitializeEx failed " + hresultHex(initResult);
        return false;
    }

    CLSID enumeratorClass{};
    IID enumeratorInterface{};
    if (FAILED(CLSIDFromString(
            L"{BCDE0395-E52F-467C-8E3D-C4579291692E}", &enumeratorClass)) ||
        FAILED(IIDFromString(
            L"{A95664D2-9614-4F35-A746-DE8DB63617E6}", &enumeratorInterface))) {
        error = "MMDeviceEnumerator CLSID/IID parse failed";
        if (uninitialize) CoUninitialize();
        return false;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    const HRESULT createResult = CoCreateInstance(
        enumeratorClass,
        nullptr,
        CLSCTX_ALL,
        enumeratorInterface,
        reinterpret_cast<void**>(&enumerator));
    if (FAILED(createResult) || !enumerator) {
        error = "CoCreateInstance(IMMDeviceEnumerator) failed " + hresultHex(createResult);
        if (uninitialize) CoUninitialize();
        return false;
    }

    IMMDeviceCollection* collection = nullptr;
    const HRESULT enumResult = enumerator->EnumAudioEndpoints(
        input ? eCapture : eRender, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(enumResult) || !collection) {
        error = "EnumAudioEndpoints failed " + hresultHex(enumResult);
        enumerator->Release();
        if (uninitialize) CoUninitialize();
        return false;
    }

    UINT count = 0;
    const HRESULT countResult = collection->GetCount(&count);
    if (FAILED(countResult)) {
        error = "IMMDeviceCollection::GetCount failed " + hresultHex(countResult);
        collection->Release();
        enumerator->Release();
        if (uninitialize) CoUninitialize();
        return false;
    }

    const std::wstring needle = lower(widen(match));
    if (needle.empty()) {
        error = "could not convert device match to UTF-16";
        collection->Release();
        enumerator->Release();
        if (uninitialize) CoUninitialize();
        return false;
    }
    const PROPERTYKEY nameKey = friendlyNamePropertyKey();
    std::vector<std::pair<std::string, std::string>> matches;
    for (UINT index = 0; index < count; ++index) {
        IMMDevice* device = nullptr;
        if (FAILED(collection->Item(index, &device)) || !device) {
            continue;
        }

        LPWSTR rawId = nullptr;
        std::wstring idWide;
        if (SUCCEEDED(device->GetId(&rawId)) && rawId) {
            idWide = rawId;
            CoTaskMemFree(rawId);
        }

        std::wstring nameWide;
        IPropertyStore* store = nullptr;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)) && store) {
            PROPVARIANT value;
            PropVariantInit(&value);
            if (SUCCEEDED(store->GetValue(nameKey, &value)) &&
                value.vt == VT_LPWSTR && value.pwszVal) {
                nameWide = value.pwszVal;
            }
            PropVariantClear(&value);
            store->Release();
        }

        if (!idWide.empty() && lower(nameWide).find(needle) != std::wstring::npos) {
            matches.emplace_back(narrow(idWide), narrow(nameWide));
        }
        device->Release();
    }

    collection->Release();
    enumerator->Release();
    if (uninitialize) CoUninitialize();

    if (matches.empty()) {
        error = "no active " + std::string(input ? "capture" : "render") +
            " device matching '" + match + "' (" + std::to_string(count) +
            " active endpoint(s))";
        return false;
    }
    if (matches.size() != 1) {
        error = "ambiguous " + std::string(input ? "capture" : "render") +
            " match '" + match + "' (" + std::to_string(matches.size()) + " matches)";
        return false;
    }

    id = std::move(matches.front().first);
    name = std::move(matches.front().second);
    return true;
}

bool replaceFile(const std::filesystem::path& source, const std::filesystem::path& target) {
    return MoveFileExW(
        source.wstring().c_str(),
        target.wstring().c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
#endif

struct DesiredDevice {
    bool ok = true;
    std::string id;
    std::string error;
};

DesiredDevice resolveDesiredDevice(
    bool input,
    const std::string& rawId,
    const std::string& rawMatch,
    bool verbose,
    const Log& log
) {
    const std::string explicitId = trim(rawId);
    const std::string match = trim(rawMatch);
    if (!explicitId.empty() && !isLegacyDefaultId(explicitId)) {
        return {true, normalizeDeviceId(input, explicitId), {}};
    }
    if (match.empty()) {
        return {};
    }

#if IBM
    std::string resolvedId;
    std::string resolvedName;
    std::string error;
    if (!resolveDeviceByName(input, match, resolvedId, resolvedName, error)) {
        emit(log, verbose, std::string(input ? "input" : "output") +
            " match failed: " + error);
        return {false, {}, std::move(error)};
    }
    emit(log, verbose, std::string(input ? "input" : "output") +
        " match '" + match + "' -> " + resolvedName);
    return {true, normalizeDeviceId(input, resolvedId), {}};
#else
    const std::string error = std::string(input ? "input" : "output") +
        " name matching is Windows-only";
    emit(log, verbose, error);
    return {false, {}, error};
#endif
}

} // namespace

std::string normalizeDeviceId(bool input, const std::string& rawId) {
    std::string id = trim(rawId);
    if (id.empty()) {
        return {};
    }
    std::string upper = upperAscii(id);
    if (upper.rfind("\\\\?\\SWD#MMDEVAPI#", 0) == 0) {
        return id;
    }
    if (upper.rfind("SWD#MMDEVAPI#", 0) == 0) {
        return "\\\\?\\" + id;
    }
    if (upper.rfind("SWD\\MMDEVAPI\\", 0) == 0) {
        std::replace(id.begin(), id.end(), '\\', '#');
        return "\\\\?\\" + id + "#" +
            (input ? kInputDeviceInterfaceGuid : kOutputDeviceInterfaceGuid);
    }
    return std::string("\\\\?\\SWD#MMDEVAPI#") + id + "#" +
        (input ? kInputDeviceInterfaceGuid : kOutputDeviceInterfaceGuid);
}

std::string updateConfigText(
    const std::string& current,
    const std::string& desiredInput,
    const std::string& desiredOutput,
    bool& changed
) {
    std::vector<std::string> lines = splitLines(current);
    bool inAudio = false;
    bool hasAudioSection = false;
    bool hasInput = false;
    bool hasOutput = false;
    std::size_t audioEnd = lines.size();
    changed = false;

    for (std::size_t index = 0; index < lines.size(); ++index) {
        const std::string stripped = trim(lines[index]);
        if (stripped.size() >= 2 && stripped.front() == '[' && stripped.back() == ']') {
            if (inAudio) {
                audioEnd = index;
            }
            inAudio = upperAscii(trim(stripped.substr(1, stripped.size() - 2))) == "AUDIO";
            if (inAudio) {
                hasAudioSection = true;
                audioEnd = lines.size();
            }
            continue;
        }
        if (!inAudio) {
            continue;
        }
        const auto separator = lines[index].find('=');
        if (separator == std::string::npos) {
            continue;
        }
        const std::string key = upperAscii(trim(lines[index].substr(0, separator)));
        if (key == "INPUT") {
            hasInput = true;
            changed |= updateLine(lines, index, "INPUT", desiredInput);
        } else if (key == "OUTPUT") {
            hasOutput = true;
            changed |= updateLine(lines, index, "OUTPUT", desiredOutput);
        }
    }

    if (!hasAudioSection) {
        if (!lines.empty() && !trim(lines.back()).empty()) {
            lines.emplace_back();
        }
        lines.emplace_back("[AUDIO]");
        if (!desiredInput.empty()) lines.emplace_back("INPUT=" + desiredInput);
        if (!desiredOutput.empty()) lines.emplace_back("OUTPUT=" + desiredOutput);
        changed = true;
    } else {
        std::vector<std::string> missing;
        if (!hasInput && !desiredInput.empty()) missing.emplace_back("INPUT=" + desiredInput);
        if (!hasOutput && !desiredOutput.empty()) missing.emplace_back("OUTPUT=" + desiredOutput);
        if (!missing.empty()) {
            lines.insert(
                lines.begin() + static_cast<std::ptrdiff_t>(audioEnd),
                missing.begin(),
                missing.end());
            changed = true;
        }
    }

    std::ostringstream output;
    for (const auto& line : lines) {
        output << line << '\n';
    }
    return output.str();
}

Result ensureConfig(
    const std::filesystem::path& path,
    const Config& config,
    bool verbose,
    const Log& log
) {
    if (!config.enabled) {
        emit(log, verbose, "disabled");
        return {Status::Disabled, "DISABLED"};
    }

    const DesiredDevice input = resolveDesiredDevice(
        true, config.inputDeviceId, config.inputDeviceMatch, verbose, log);
    const DesiredDevice output = resolveDesiredDevice(
        false, config.outputDeviceId, config.outputDeviceMatch, verbose, log);
    if (!input.ok || !output.ok) {
        return {Status::Failed, !input.ok ? input.error : output.error};
    }
    if (input.id.empty() && output.id.empty()) {
        emit(log, verbose, "no desired INPUT/OUTPUT configured");
        return {Status::NoDesiredDevices, "NO_DESIRED_DEVICES"};
    }

    std::string current;
    bool existed = false;
    {
        std::ifstream stream(path, std::ios::binary);
        if (stream.is_open()) {
            existed = true;
            current.assign(
                std::istreambuf_iterator<char>(stream),
                std::istreambuf_iterator<char>());
        }
    }

    bool changed = false;
    const std::string updated = updateConfigText(current, input.id, output.id, changed);
    if (!changed) {
        emit(log, verbose, "OK (" + path.string() + ")");
        return {Status::Unchanged, "UNCHANGED"};
    }

    std::error_code directoryError;
    std::filesystem::create_directories(path.parent_path(), directoryError);
    if (directoryError) {
        const std::string detail = "cannot create directory: " + directoryError.message();
        emit(log, true, detail);
        return {Status::Failed, detail};
    }

    std::filesystem::path temporary = path;
    temporary += ".yal-autounicomhelper.tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream.is_open()) {
            const std::string detail = "cannot write " + temporary.string();
            emit(log, true, detail);
            return {Status::Failed, detail};
        }
        stream.write(updated.data(), static_cast<std::streamsize>(updated.size()));
        if (!stream.good()) {
            const std::string detail = "write failed " + temporary.string();
            emit(log, true, detail);
            stream.close();
            std::error_code cleanupError;
            std::filesystem::remove(temporary, cleanupError);
            return {Status::Failed, detail};
        }
    }

    bool replaced = false;
#if IBM
    replaced = replaceFile(temporary, path);
#else
    replaced = std::rename(temporary.string().c_str(), path.string().c_str()) == 0;
#endif
    if (!replaced) {
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        const std::string detail = "replace failed for " + path.string();
        emit(log, true, detail);
        return {Status::Failed, detail};
    }

    emit(log, true, std::string(existed ? "updated " : "created ") + path.string());
    return {Status::Updated, existed ? "UPDATED" : "CREATED"};
}

} // namespace altitude_audio_guard
