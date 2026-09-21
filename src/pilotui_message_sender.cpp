#include "pilotui_message_sender.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cwctype>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#if IBM
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <UIAutomation.h>
#endif

namespace pilotui_message {

void ComposerOwnershipTracker::beginWrite(
    std::string expectedText,
    std::int64_t startedAtMs
) {
    expectedText_ = std::move(expectedText);
    startedAtMs_ = startedAtMs;
    active_ = !expectedText_.empty();
    composed_ = false;
}

void ComposerOwnershipTracker::markComposed() {
    if (active_) {
        composed_ = true;
    }
}

void ComposerOwnershipTracker::clear() {
    expectedText_.clear();
    startedAtMs_ = 0;
    active_ = false;
    composed_ = false;
}

bool ComposerOwnershipTracker::active() const {
    return active_;
}

bool ComposerOwnershipTracker::recoveryDue(std::int64_t nowMs, int staleMs) const {
    return active_ && nowMs >= startedAtMs_ && nowMs - startedAtMs_ >= staleMs;
}

ComposerOwnershipMatch ComposerOwnershipTracker::classify(
    const std::string& currentText
) const {
    if (currentText.empty()) {
        return ComposerOwnershipMatch::Empty;
    }
    if (!active_) {
        return ComposerOwnershipMatch::Foreign;
    }
    if (currentText == expectedText_) {
        return ComposerOwnershipMatch::Exact;
    }
    if (!composed_ && currentText.size() < expectedText_.size() &&
        expectedText_.compare(0, currentText.size(), currentText) == 0) {
        return ComposerOwnershipMatch::Prefix;
    }
    return ComposerOwnershipMatch::Foreign;
}

namespace {

std::mutex g_composerOwnershipMutex;
ComposerOwnershipTracker g_composerOwnership;

std::int64_t steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

void beginComposerOwnership(const std::string& expectedText) {
    std::lock_guard<std::mutex> lock(g_composerOwnershipMutex);
    g_composerOwnership.beginWrite(expectedText, steadyNowMs());
}

void clearComposerOwnership() {
    std::lock_guard<std::mutex> lock(g_composerOwnershipMutex);
    g_composerOwnership.clear();
}

void markComposerComposed() {
    std::lock_guard<std::mutex> lock(g_composerOwnershipMutex);
    g_composerOwnership.markComposed();
}

ComposerOwnershipMatch classifyComposerOwnership(const std::string& currentText) {
    std::lock_guard<std::mutex> lock(g_composerOwnershipMutex);
    return g_composerOwnership.classify(currentText);
}

void log(const Callbacks& callbacks, const std::string& line) {
    if (callbacks.log) {
        callbacks.log(line);
    }
}

bool shouldStop(const Callbacks& callbacks) {
    return callbacks.shouldStop && callbacks.shouldStop();
}

bool waitForComposerRetry(const Callbacks& callbacks, int delayMs) {
    int remainingMs = std::max(0, delayMs);
    while (remainingMs > 0) {
        if (shouldStop(callbacks)) {
            return false;
        }
        const int chunkMs = std::min(remainingMs, 50);
        std::this_thread::sleep_for(std::chrono::milliseconds(chunkMs));
        remainingMs -= chunkMs;
    }
    return !shouldStop(callbacks);
}

#if IBM

template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    explicit ComPtr(T* value) : value_(value) {}
    ~ComPtr() { reset(); }

    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    ComPtr(ComPtr&& other) noexcept : value_(other.value_) {
        other.value_ = nullptr;
    }

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

    void reset(T* value = nullptr) {
        if (value_) {
            value_->Release();
        }
        value_ = value;
    }

private:
    T* value_ = nullptr;
};

std::wstring widen(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

std::string narrow(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring bstrToWstring(BSTR value) {
    std::wstring out = value ? std::wstring(value, SysStringLen(value)) : std::wstring();
    if (value) {
        SysFreeString(value);
    }
    return out;
}

std::wstring trimWide(std::wstring value) {
    auto notSpace = [](wchar_t c) { return std::iswspace(c) == 0; };
    auto first = std::find_if(value.begin(), value.end(), notSpace);
    auto last = std::find_if(value.rbegin(), value.rend(), notSpace).base();
    if (first >= last) {
        return {};
    }
    return std::wstring(first, last);
}

std::wstring lowerWide(std::wstring value) {
    for (wchar_t& c : value) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return value;
}

bool exactName(const std::wstring& actual, const std::wstring& expected) {
    return lowerWide(trimWide(actual)) == lowerWide(trimWide(expected));
}

std::wstring simplifyWide(const std::wstring& value) {
    std::wstring out;
    out.reserve(value.size());
    bool space = false;
    for (wchar_t c : value) {
        if (std::iswspace(c)) {
            space = !out.empty();
            continue;
        }
        if (space) {
            out.push_back(L' ');
            space = false;
        }
        out.push_back(c);
    }
    return out;
}

struct WindowSearch {
    std::wstring title;
    HWND handle = nullptr;
    int matches = 0;
};

BOOL CALLBACK enumWindows(HWND handle, LPARAM refcon) {
    auto* search = reinterpret_cast<WindowSearch*>(refcon);
    if (!search) {
        return TRUE;
    }
    wchar_t title[512] = {};
    if (GetWindowTextW(handle, title, 511) <= 0) {
        return TRUE;
    }
    if (lowerWide(title).find(lowerWide(search->title)) == std::wstring::npos) {
        return TRUE;
    }
    ++search->matches;
    search->handle = handle;
    return TRUE;
}

bool findUniqueWindow(const std::wstring& title, HWND& handle, std::string& error) {
    WindowSearch search{title, nullptr, 0};
    EnumWindows(enumWindows, reinterpret_cast<LPARAM>(&search));
    if (search.matches == 0) {
        error = "PILOTUI_WINDOW_MISSING";
        return false;
    }
    if (search.matches != 1) {
        error = "PILOTUI_WINDOW_AMBIGUOUS";
        return false;
    }
    handle = search.handle;
    return true;
}

bool getName(IUIAutomationElement* element, std::wstring& name) {
    BSTR value = nullptr;
    if (!element || FAILED(element->get_CurrentName(&value))) {
        return false;
    }
    name = bstrToWstring(value);
    return true;
}

bool getRect(IUIAutomationElement* element, RECT& rect) {
    rect = {};
    if (!element || FAILED(element->get_CurrentBoundingRectangle(&rect))) {
        return false;
    }
    return rect.right > rect.left && rect.bottom > rect.top;
}

bool createTypeCondition(
    IUIAutomation* automation,
    CONTROLTYPEID type,
    ComPtr<IUIAutomationCondition>& condition
) {
    VARIANT value;
    VariantInit(&value);
    value.vt = VT_I4;
    value.lVal = type;
    return automation &&
        SUCCEEDED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, value, condition.put())) &&
        condition;
}

bool findAllByType(
    IUIAutomation* automation,
    IUIAutomationElement* root,
    CONTROLTYPEID type,
    ComPtr<IUIAutomationElementArray>& elements
) {
    ComPtr<IUIAutomationCondition> condition;
    if (!createTypeCondition(automation, type, condition)) {
        return false;
    }
    return SUCCEEDED(root->FindAll(TreeScope_Subtree, condition.get(), elements.put())) && elements;
}

std::wstring getStringProperty(IUIAutomationElement* element, PROPERTYID property) {
    if (!element) {
        return {};
    }
    VARIANT value;
    VariantInit(&value);
    if (FAILED(element->GetCurrentPropertyValue(property, &value))) {
        VariantClear(&value);
        return {};
    }
    std::wstring out;
    if (value.vt == VT_BSTR && value.bstrVal) {
        out.assign(value.bstrVal, SysStringLen(value.bstrVal));
    }
    VariantClear(&value);
    return out;
}

std::string quoteDiagnostic(const std::wstring& value) {
    std::string text = narrow(value);
    for (char& c : text) {
        if (c == '\r' || c == '\n' || c == '\t') {
            c = ' ';
        }
    }
    return "\"" + text + "\"";
}

std::string rectDiagnostic(const RECT& rect) {
    return std::to_string(rect.left) + "," + std::to_string(rect.top) + "," +
        std::to_string(rect.right) + "," + std::to_string(rect.bottom);
}

struct ElementDiagnostic {
    CONTROLTYPEID type = 0;
    std::wstring name;
    std::wstring automationId;
    std::wstring className;
    RECT rect{};
    bool hasRect = false;
    bool keyboardFocusable = false;
    bool hasKeyboardFocus = false;
    bool offscreen = true;
    bool valuePattern = false;
    bool valueReadOnly = true;
};

ElementDiagnostic inspectElement(IUIAutomationElement* element) {
    ElementDiagnostic out{};
    if (!element) {
        return out;
    }
    element->get_CurrentControlType(&out.type);
    getName(element, out.name);
    out.automationId = getStringProperty(element, UIA_AutomationIdPropertyId);
    out.className = getStringProperty(element, UIA_ClassNamePropertyId);
    out.hasRect = getRect(element, out.rect);

    BOOL value = FALSE;
    if (SUCCEEDED(element->get_CurrentIsKeyboardFocusable(&value))) {
        out.keyboardFocusable = value != FALSE;
    }
    value = FALSE;
    if (SUCCEEDED(element->get_CurrentHasKeyboardFocus(&value))) {
        out.hasKeyboardFocus = value != FALSE;
    }
    value = TRUE;
    if (SUCCEEDED(element->get_CurrentIsOffscreen(&value))) {
        out.offscreen = value != FALSE;
    }

    IUIAutomationValuePattern* rawPattern = nullptr;
    HRESULT patternHr = element->GetCurrentPatternAs(
        UIA_ValuePatternId,
        IID_PPV_ARGS(&rawPattern)
    );
    ComPtr<IUIAutomationValuePattern> pattern(rawPattern);
    if (SUCCEEDED(patternHr) && pattern) {
        out.valuePattern = true;
        value = TRUE;
        if (SUCCEEDED(pattern->get_CurrentIsReadOnly(&value))) {
            out.valueReadOnly = value != FALSE;
        }
    }
    return out;
}

void logElementDiagnostic(
    const Callbacks& callbacks,
    const std::string& label,
    const ElementDiagnostic& element
) {
    log(callbacks,
        "Auto UNICOM UIA: " + label +
        " type=" + std::to_string(element.type) +
        " name=" + quoteDiagnostic(element.name) +
        " automation_id=" + quoteDiagnostic(element.automationId) +
        " class=" + quoteDiagnostic(element.className) +
        " rect=" + (element.hasRect ? rectDiagnostic(element.rect) : std::string("none")) +
        " focusable=" + std::to_string(element.keyboardFocusable ? 1 : 0) +
        " focused=" + std::to_string(element.hasKeyboardFocus ? 1 : 0) +
        " offscreen=" + std::to_string(element.offscreen ? 1 : 0) +
        " value_pattern=" + std::to_string(element.valuePattern ? 1 : 0) +
        " readonly=" + std::to_string(element.valueReadOnly ? 1 : 0));
}

void logComposerDiagnostics(
    IUIAutomation* automation,
    IUIAutomationElement* root,
    const Callbacks& callbacks
) {
    if (!automation || !root) {
        return;
    }

    ComPtr<IUIAutomationElement> focused;
    if (SUCCEEDED(automation->GetFocusedElement(focused.put())) && focused) {
        logElementDiagnostic(callbacks, "focused", inspectElement(focused.get()));
    } else {
        log(callbacks, "Auto UNICOM UIA: focused element unavailable");
    }

    ComPtr<IUIAutomationCondition> condition;
    if (FAILED(automation->CreateTrueCondition(condition.put())) || !condition) {
        log(callbacks, "Auto UNICOM UIA: composer diagnostic condition failed");
        return;
    }
    ComPtr<IUIAutomationElementArray> elements;
    if (FAILED(root->FindAll(TreeScope_Subtree, condition.get(), elements.put())) || !elements) {
        log(callbacks, "Auto UNICOM UIA: composer diagnostic scan failed");
        return;
    }

    int count = 0;
    elements->get_Length(&count);
    int candidates = 0;
    for (int i = 0; i < count; ++i) {
        ComPtr<IUIAutomationElement> element;
        if (FAILED(elements->GetElement(i, element.put())) || !element) {
            continue;
        }
        ElementDiagnostic diagnostic = inspectElement(element.get());
        const std::wstring lowerName = lowerWide(diagnostic.name);
        const std::wstring lowerId = lowerWide(diagnostic.automationId);
        const std::wstring lowerClass = lowerWide(diagnostic.className);
        const bool messageHint = lowerName.find(L"message") != std::wstring::npos ||
            lowerId.find(L"message") != std::wstring::npos ||
            lowerClass.find(L"message") != std::wstring::npos;
        const bool candidate = diagnostic.type == UIA_EditControlTypeId ||
            (diagnostic.valuePattern && !diagnostic.valueReadOnly) ||
            diagnostic.hasKeyboardFocus ||
            (diagnostic.keyboardFocusable &&
                (diagnostic.type == UIA_CustomControlTypeId ||
                 diagnostic.type == UIA_DocumentControlTypeId ||
                 diagnostic.type == UIA_PaneControlTypeId ||
                 diagnostic.type == UIA_TextControlTypeId)) ||
            messageHint;
        if (!candidate) {
            continue;
        }
        ++candidates;
        logElementDiagnostic(
            callbacks,
            "composer_candidate[" + std::to_string(candidates) + "]",
            diagnostic);
    }
    log(callbacks, "Auto UNICOM UIA: composer diagnostic candidates=" +
        std::to_string(candidates) + " elements=" + std::to_string(count));
}

bool findComposer(
    IUIAutomation* automation,
    IUIAutomationElement* root,
    const std::wstring& expectedName,
    ComPtr<IUIAutomationElement>& composer,
    ComPtr<IUIAutomationValuePattern>& valuePattern,
    RECT& rect,
    std::string& error
) {
    ComPtr<IUIAutomationElementArray> elements;
    if (!findAllByType(automation, root, UIA_EditControlTypeId, elements)) {
        error = "COMPOSER_SCAN_FAILED";
        return false;
    }

    std::vector<ComPtr<IUIAutomationElement>> namedMatches;
    std::vector<ComPtr<IUIAutomationElement>> qtTextFieldMatches;
    int count = 0;
    elements->get_Length(&count);
    for (int i = 0; i < count; ++i) {
        ComPtr<IUIAutomationElement> element;
        if (FAILED(elements->GetElement(i, element.put())) || !element) {
            continue;
        }
        std::wstring name;
        getName(element.get(), name);
        IUIAutomationValuePattern* rawPattern = nullptr;
        HRESULT patternHr = element->GetCurrentPatternAs(
            UIA_ValuePatternId,
            IID_PPV_ARGS(&rawPattern)
        );
        ComPtr<IUIAutomationValuePattern> candidatePattern(rawPattern);
        if (FAILED(patternHr) || !candidatePattern) {
            continue;
        }
        BOOL readOnly = TRUE;
        if (FAILED(candidatePattern->get_CurrentIsReadOnly(&readOnly)) || readOnly) {
            continue;
        }
        BOOL enabled = FALSE;
        if (FAILED(element->get_CurrentIsEnabled(&enabled)) || !enabled) {
            continue;
        }
        BOOL offscreen = TRUE;
        if (FAILED(element->get_CurrentIsOffscreen(&offscreen)) || offscreen) {
            continue;
        }
        RECT candidateRect{};
        if (!getRect(element.get(), candidateRect)) {
            continue;
        }

        if (exactName(name, expectedName)) {
            namedMatches.push_back(std::move(element));
            continue;
        }

        const std::wstring className = lowerWide(
            getStringProperty(element.get(), UIA_ClassNamePropertyId));
        if (trimWide(name).empty() && className.find(L"textfield") != std::wstring::npos) {
            qtTextFieldMatches.push_back(std::move(element));
        }
    }

    if (namedMatches.size() > 1) {
        error = "COMPOSER_AMBIGUOUS";
        return false;
    }
    if (namedMatches.size() == 1) {
        composer = std::move(namedMatches.front());
    } else if (qtTextFieldMatches.size() > 1) {
        error = "COMPOSER_AMBIGUOUS";
        return false;
    } else if (qtTextFieldMatches.size() == 1) {
        // Altitude exposes the QML placeholder, but not "Message", as UIA Name.
        composer = std::move(qtTextFieldMatches.front());
    } else {
        error = "COMPOSER_MISSING";
        return false;
    }

    IUIAutomationValuePattern* rawPattern = nullptr;
    HRESULT patternHr = composer->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&rawPattern));
    valuePattern.reset(rawPattern);
    if (FAILED(patternHr) || !valuePattern) {
        error = "COMPOSER_VALUE_PATTERN_MISSING";
        return false;
    }
    if (!getRect(composer.get(), rect)) {
        error = "COMPOSER_RECT_MISSING";
        return false;
    }
    return true;
}

int verticalOverlap(const RECT& left, const RECT& right) {
    return std::max(0L, std::min(left.bottom, right.bottom) - std::max(left.top, right.top));
}

int horizontalOverlap(const RECT& left, const RECT& right) {
    return std::max(0L, std::min(left.right, right.right) - std::max(left.left, right.left));
}

bool findSendButton(
    IUIAutomation* automation,
    IUIAutomationElement* root,
    const std::wstring& expectedName,
    const RECT& composerRect,
    ComPtr<IUIAutomationElement>& button,
    ComPtr<IUIAutomationInvokePattern>& invokePattern,
    std::string& error
) {
    ComPtr<IUIAutomationElementArray> elements;
    if (!findAllByType(automation, root, UIA_ButtonControlTypeId, elements)) {
        error = "SEND_SCAN_FAILED";
        return false;
    }

    std::vector<ComPtr<IUIAutomationElement>> matches;
    int count = 0;
    elements->get_Length(&count);
    for (int i = 0; i < count; ++i) {
        ComPtr<IUIAutomationElement> element;
        if (FAILED(elements->GetElement(i, element.put())) || !element) {
            continue;
        }
        std::wstring name;
        if (!getName(element.get(), name) || !exactName(name, expectedName)) {
            continue;
        }
        RECT rect{};
        if (!getRect(element.get(), rect) || verticalOverlap(rect, composerRect) <= 0 ||
            rect.left < composerRect.left + (composerRect.right - composerRect.left) / 2) {
            continue;
        }
        IUIAutomationInvokePattern* rawPattern = nullptr;
        HRESULT patternHr = element->GetCurrentPatternAs(
            UIA_InvokePatternId,
            IID_PPV_ARGS(&rawPattern)
        );
        ComPtr<IUIAutomationInvokePattern> candidatePattern(rawPattern);
        if (FAILED(patternHr) || !candidatePattern) {
            continue;
        }
        matches.push_back(std::move(element));
    }

    if (matches.empty()) {
        error = "SEND_BUTTON_MISSING";
        return false;
    }
    if (matches.size() != 1) {
        error = "SEND_BUTTON_AMBIGUOUS";
        return false;
    }

    button = std::move(matches.front());
    IUIAutomationInvokePattern* rawPattern = nullptr;
    HRESULT patternHr = button->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&rawPattern));
    invokePattern.reset(rawPattern);
    if (FAILED(patternHr) || !invokePattern) {
        error = "SEND_INVOKE_PATTERN_MISSING";
        return false;
    }
    return true;
}

bool readValue(IUIAutomationElement* element, std::wstring& value) {
    if (!element) {
        return false;
    }
    IUIAutomationValuePattern* rawPattern = nullptr;
    HRESULT patternHr = element->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&rawPattern));
    ComPtr<IUIAutomationValuePattern> pattern(rawPattern);
    if (FAILED(patternHr) || !pattern) {
        return false;
    }
    BSTR raw = nullptr;
    if (FAILED(pattern->get_CurrentValue(&raw))) {
        return false;
    }
    value = bstrToWstring(raw);
    return true;
}

bool readText(IUIAutomationElement* element, std::wstring& value) {
    if (!element) {
        return false;
    }
    IUIAutomationTextPattern* rawPattern = nullptr;
    HRESULT patternHr = element->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&rawPattern));
    ComPtr<IUIAutomationTextPattern> pattern(rawPattern);
    if (FAILED(patternHr) || !pattern) {
        return false;
    }
    ComPtr<IUIAutomationTextRange> range;
    if (FAILED(pattern->get_DocumentRange(range.put())) || !range) {
        return false;
    }
    BSTR raw = nullptr;
    if (FAILED(range->GetText(-1, &raw))) {
        return false;
    }
    value = bstrToWstring(raw);
    return true;
}

int countOccurrences(const std::wstring& text, const std::wstring& needle) {
    if (text.empty() || needle.empty()) {
        return 0;
    }
    int count = 0;
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::wstring::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

struct HistoryScan {
    bool readable = false;
    int surfaces = 0;
    int matches = 0;
};

bool isHistoryControlType(CONTROLTYPEID type) {
    return type == UIA_TextControlTypeId ||
        type == UIA_EditControlTypeId ||
        type == UIA_DocumentControlTypeId ||
        type == UIA_ListControlTypeId ||
        type == UIA_ListItemControlTypeId ||
        type == UIA_DataItemControlTypeId ||
        type == UIA_PaneControlTypeId;
}

HistoryScan scanHistory(
    IUIAutomation* automation,
    IUIAutomationElement* root,
    IUIAutomationElement* composer,
    const RECT& composerRect,
    const std::wstring& message
) {
    HistoryScan result{};
    ComPtr<IUIAutomationCondition> condition;
    if (FAILED(automation->CreateTrueCondition(condition.put())) || !condition) {
        return result;
    }
    ComPtr<IUIAutomationElementArray> elements;
    if (FAILED(root->FindAll(TreeScope_Subtree, condition.get(), elements.put())) || !elements) {
        return result;
    }

    int count = 0;
    elements->get_Length(&count);
    for (int i = 0; i < count; ++i) {
        ComPtr<IUIAutomationElement> element;
        if (FAILED(elements->GetElement(i, element.put())) || !element) {
            continue;
        }
        BOOL same = FALSE;
        if (SUCCEEDED(automation->CompareElements(element.get(), composer, &same)) && same) {
            continue;
        }
        CONTROLTYPEID type = 0;
        if (FAILED(element->get_CurrentControlType(&type)) || !isHistoryControlType(type)) {
            continue;
        }
        RECT rect{};
        if (!getRect(element.get(), rect) || rect.bottom > composerRect.top + 8 ||
            horizontalOverlap(rect, composerRect) <= 0) {
            continue;
        }

        ++result.surfaces;
        std::unordered_set<std::wstring> values;
        std::wstring value;
        if (readText(element.get(), value)) {
            values.insert(simplifyWide(value));
            result.readable = true;
        }
        if (readValue(element.get(), value)) {
            values.insert(simplifyWide(value));
            result.readable = true;
        }
        if (getName(element.get(), value)) {
            values.insert(simplifyWide(value));
            result.readable = true;
        }
        for (const auto& candidate : values) {
            result.matches += countOccurrences(candidate, message);
        }
    }
    return result;
}

bool currentComposerValue(IUIAutomationValuePattern* pattern, std::wstring& value) {
    if (!pattern) {
        return false;
    }
    BSTR raw = nullptr;
    if (FAILED(pattern->get_CurrentValue(&raw))) {
        return false;
    }
    value = bstrToWstring(raw);
    return true;
}

HRESULT setComposerValue(IUIAutomationValuePattern* pattern, const std::wstring& value) {
    if (!pattern) {
        return E_POINTER;
    }
    BSTR raw = SysAllocStringLen(value.data(), static_cast<UINT>(value.size()));
    if (!raw && !value.empty()) {
        return E_OUTOFMEMORY;
    }
    HRESULT result = pattern->SetValue(raw);
    SysFreeString(raw);
    return result;
}

ComposerOwnershipMatch classifyCurrentComposer(const std::wstring& current) {
    return classifyComposerOwnership(narrow(simplifyWide(current)));
}

bool clearTrackedComposer(IUIAutomationValuePattern* pattern) {
    std::wstring current;
    if (!currentComposerValue(pattern, current)) {
        return false;
    }
    const auto match = classifyCurrentComposer(current);
    if (match == ComposerOwnershipMatch::Empty) {
        clearComposerOwnership();
        return true;
    }
    if (match == ComposerOwnershipMatch::Foreign) {
        clearComposerOwnership();
        return false;
    }
    if (FAILED(setComposerValue(pattern, L""))) {
        return false;
    }
    if (!currentComposerValue(pattern, current) || !simplifyWide(current).empty()) {
        return false;
    }
    clearComposerOwnership();
    return true;
}

void reconcileComposerOwnership(IUIAutomationValuePattern* pattern) {
    std::wstring current;
    if (!currentComposerValue(pattern, current)) {
        return;
    }
    const auto match = classifyCurrentComposer(current);
    if (match == ComposerOwnershipMatch::Empty ||
        match == ComposerOwnershipMatch::Foreign) {
        clearComposerOwnership();
    }
}

Result submitWindows(const Options& options, const Callbacks& callbacks) {
    if (shouldStop(callbacks)) {
        return {Status::Cancelled, "CANCELLED_BEFORE_DISCOVERY"};
    }

    HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize = SUCCEEDED(initHr);
    if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE) {
        return {Status::FailedBeforeSubmit, "COM_INITIALIZE_FAILED"};
    }
    struct CoGuard {
        bool active = false;
        ~CoGuard() { if (active) CoUninitialize(); }
    } coGuard{uninitialize};

    IUIAutomation* rawAutomation = nullptr;
    HRESULT automationHr = CoCreateInstance(
            CLSID_CUIAutomation,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&rawAutomation));
    ComPtr<IUIAutomation> automation(rawAutomation);
    if (FAILED(automationHr) || !automation) {
        return {Status::FailedBeforeSubmit, "UIA_INITIALIZE_FAILED"};
    }

    HWND window = nullptr;
    std::string error;
    if (!findUniqueWindow(widen(options.windowTitle), window, error)) {
        return {Status::FailedBeforeSubmit, std::move(error)};
    }
    ComPtr<IUIAutomationElement> root;
    if (FAILED(automation->ElementFromHandle(window, root.put())) || !root) {
        return {Status::FailedBeforeSubmit, "PILOTUI_ROOT_FAILED"};
    }

    ComPtr<IUIAutomationElement> composer;
    ComPtr<IUIAutomationValuePattern> composerPattern;
    RECT composerRect{};
    if (!findComposer(
            automation.get(), root.get(), widen(options.composerName),
            composer, composerPattern, composerRect, error)) {
        return {Status::FailedBeforeSubmit, std::move(error)};
    }

    ComPtr<IUIAutomationElement> sendButton;
    ComPtr<IUIAutomationInvokePattern> invokePattern;
    if (!findSendButton(
            automation.get(), root.get(), widen(options.sendButtonName), composerRect,
            sendButton, invokePattern, error)) {
        return {Status::FailedBeforeSubmit, std::move(error)};
    }

    const std::wstring message = simplifyWide(widen(options.message));
    HistoryScan baseline = scanHistory(automation.get(), root.get(), composer.get(), composerRect, message);
    if (!baseline.readable || baseline.surfaces == 0) {
        return {Status::FailedBeforeSubmit, "ACTIVE_HISTORY_UNAVAILABLE"};
    }
    if (options.debug) {
        log(callbacks, "Auto UNICOM UIA: composer and SEND resolved; history_surfaces=" +
            std::to_string(baseline.surfaces) + " baseline_matches=" + std::to_string(baseline.matches));
    }

    std::wstring current;
    if (!currentComposerValue(composerPattern.get(), current)) {
        return {Status::FailedBeforeSubmit, "COMPOSER_READ_FAILED"};
    }
    if (!simplifyWide(current).empty()) {
        return {Status::FailedBeforeSubmit, "COMPOSER_NOT_EMPTY"};
    }
    clearComposerOwnership();
    if (shouldStop(callbacks)) {
        return {Status::Cancelled, "CANCELLED_BEFORE_COMPOSE"};
    }
    beginComposerOwnership(narrow(message));
    if (FAILED(setComposerValue(composerPattern.get(), message))) {
        clearComposerOwnership();
        return {Status::FailedBeforeSubmit, "COMPOSER_WRITE_FAILED"};
    }
    if (!currentComposerValue(composerPattern.get(), current) || simplifyWide(current) != message) {
        reconcileComposerOwnership(composerPattern.get());
        return {Status::FailedBeforeSubmit, "COMPOSER_RACE_AFTER_WRITE", baseline.matches, baseline.matches};
    }
    markComposerComposed();

    std::string gateDetail;
    if (!callbacks.finalGate || !callbacks.finalGate(gateDetail)) {
        clearTrackedComposer(composerPattern.get());
        return {Status::RejectedByGate, gateDetail.empty() ? "FINAL_GATE_REJECTED" : gateDetail,
            baseline.matches, baseline.matches};
    }
    if (shouldStop(callbacks)) {
        clearTrackedComposer(composerPattern.get());
        return {Status::Cancelled, "CANCELLED_BEFORE_SUBMIT", baseline.matches, baseline.matches};
    }

    if (!currentComposerValue(composerPattern.get(), current) || simplifyWide(current) != message) {
        clearTrackedComposer(composerPattern.get());
        return {Status::FailedBeforeSubmit, "COMPOSER_RACE_BEFORE_SUBMIT", baseline.matches, baseline.matches};
    }
    BOOL enabled = FALSE;
    if (FAILED(sendButton->get_CurrentIsEnabled(&enabled)) || !enabled) {
        clearTrackedComposer(composerPattern.get());
        return {Status::FailedBeforeSubmit, "SEND_BUTTON_DISABLED_AFTER_COMPOSE", baseline.matches, baseline.matches};
    }

    HRESULT invokeHr = invokePattern->Invoke();
    if (FAILED(invokeHr)) {
        reconcileComposerOwnership(composerPattern.get());
        return {Status::UncertainAfterSubmit, "SEND_INVOKE_INDETERMINATE", baseline.matches, baseline.matches};
    }
    log(callbacks, "Auto UNICOM UIA: SEND invoked once; awaiting visible history");

    const int timeoutMs = std::max(500, options.confirmTimeoutMs);
    const int pollMs = std::max(50, options.pollMs);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    HistoryScan latest = baseline;
    while (std::chrono::steady_clock::now() < deadline) {
        if (shouldStop(callbacks)) {
            reconcileComposerOwnership(composerPattern.get());
            return {Status::UncertainAfterSubmit, "STOPPED_AFTER_SEND", baseline.matches, latest.matches};
        }
        latest = scanHistory(automation.get(), root.get(), composer.get(), composerRect, message);
        if (latest.readable && latest.matches > baseline.matches) {
            log(callbacks, "Auto UNICOM UIA: visible history confirmed baseline_matches=" +
                std::to_string(baseline.matches) + " final_matches=" + std::to_string(latest.matches));
            clearTrackedComposer(composerPattern.get());
            return {Status::SubmittedVisible, "SUBMITTED_VISIBLE", baseline.matches, latest.matches};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(pollMs));
    }
    log(callbacks, "Auto UNICOM UIA: visible history timeout baseline_matches=" +
        std::to_string(baseline.matches) + " final_matches=" + std::to_string(latest.matches));
    reconcileComposerOwnership(composerPattern.get());
    return {Status::UncertainAfterSubmit, "VISIBLE_HISTORY_TIMEOUT", baseline.matches, latest.matches};
}

RecoveryResult recoverWindows(const Options& options, const Callbacks& callbacks) {
    if (shouldStop(callbacks)) {
        return {RecoveryStatus::Deferred, "RECOVERY_CANCELLED"};
    }

    HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize = SUCCEEDED(initHr);
    if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE) {
        return {RecoveryStatus::Failed, "COM_INITIALIZE_FAILED"};
    }
    struct CoGuard {
        bool active = false;
        ~CoGuard() { if (active) CoUninitialize(); }
    } coGuard{uninitialize};

    IUIAutomation* rawAutomation = nullptr;
    HRESULT automationHr = CoCreateInstance(
        CLSID_CUIAutomation,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&rawAutomation)
    );
    ComPtr<IUIAutomation> automation(rawAutomation);
    if (FAILED(automationHr) || !automation) {
        return {RecoveryStatus::Failed, "UIA_INITIALIZE_FAILED"};
    }

    HWND window = nullptr;
    std::string error;
    if (!findUniqueWindow(widen(options.windowTitle), window, error)) {
        return {RecoveryStatus::Deferred, std::move(error)};
    }
    ComPtr<IUIAutomationElement> root;
    if (FAILED(automation->ElementFromHandle(window, root.put())) || !root) {
        return {RecoveryStatus::Deferred, "PILOTUI_ROOT_FAILED"};
    }

    ComPtr<IUIAutomationElement> composer;
    ComPtr<IUIAutomationValuePattern> composerPattern;
    RECT composerRect{};
    if (!findComposer(
            automation.get(), root.get(), widen(options.composerName),
            composer, composerPattern, composerRect, error)) {
        return {RecoveryStatus::Deferred, std::move(error)};
    }

    std::wstring firstValue;
    if (!currentComposerValue(composerPattern.get(), firstValue)) {
        return {RecoveryStatus::Deferred, "COMPOSER_READ_FAILED"};
    }
    const auto firstMatch = classifyCurrentComposer(firstValue);
    if (firstMatch == ComposerOwnershipMatch::Empty) {
        clearComposerOwnership();
        return {RecoveryStatus::ComposerEmpty, "OWNED_COMPOSER_ALREADY_EMPTY"};
    }
    if (firstMatch == ComposerOwnershipMatch::Foreign) {
        clearComposerOwnership();
        return {RecoveryStatus::ForeignPreserved, "FOREIGN_COMPOSER_PRESERVED"};
    }

    BOOL focused = FALSE;
    if (FAILED(composer->get_CurrentHasKeyboardFocus(&focused)) || focused) {
        return {RecoveryStatus::Deferred,
            focused ? "OWNED_COMPOSER_FOCUSED" : "COMPOSER_FOCUS_UNKNOWN"};
    }

    const int settleMs = std::max(50, options.pollMs);
    if (!waitForComposerRetry(callbacks, settleMs)) {
        return {RecoveryStatus::Deferred, "RECOVERY_CANCELLED"};
    }

    std::wstring secondValue;
    if (!currentComposerValue(composerPattern.get(), secondValue)) {
        return {RecoveryStatus::Deferred, "COMPOSER_READ_FAILED"};
    }
    if (simplifyWide(secondValue) != simplifyWide(firstValue)) {
        return {RecoveryStatus::Deferred, "OWNED_COMPOSER_CHANGING"};
    }
    const auto secondMatch = classifyCurrentComposer(secondValue);
    if (secondMatch != ComposerOwnershipMatch::Exact &&
        secondMatch != ComposerOwnershipMatch::Prefix) {
        if (secondMatch == ComposerOwnershipMatch::Empty) {
            clearComposerOwnership();
            return {RecoveryStatus::ComposerEmpty, "OWNED_COMPOSER_ALREADY_EMPTY"};
        }
        clearComposerOwnership();
        return {RecoveryStatus::ForeignPreserved, "FOREIGN_COMPOSER_PRESERVED"};
    }
    focused = FALSE;
    if (FAILED(composer->get_CurrentHasKeyboardFocus(&focused)) || focused) {
        return {RecoveryStatus::Deferred,
            focused ? "OWNED_COMPOSER_FOCUSED" : "COMPOSER_FOCUS_UNKNOWN"};
    }

    if (FAILED(setComposerValue(composerPattern.get(), L""))) {
        return {RecoveryStatus::Failed, "OWNED_COMPOSER_CLEAR_FAILED"};
    }
    std::wstring clearedValue;
    if (!currentComposerValue(composerPattern.get(), clearedValue) ||
        !simplifyWide(clearedValue).empty()) {
        return {RecoveryStatus::Failed, "OWNED_COMPOSER_CLEAR_UNCONFIRMED"};
    }
    clearComposerOwnership();
    return {
        RecoveryStatus::Cleared,
        secondMatch == ComposerOwnershipMatch::Exact
            ? "STALE_OWNED_COMPOSER_CLEARED"
            : "STALE_PARTIAL_COMPOSER_CLEARED"
    };
}

DiscoveryResult discoverWindows(const Options& options, const Callbacks& callbacks) {
    if (shouldStop(callbacks)) {
        return {false, "DISCOVERY_CANCELLED"};
    }

    HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize = SUCCEEDED(initHr);
    if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE) {
        return {false, "COM_INITIALIZE_FAILED"};
    }
    struct CoGuard {
        bool active = false;
        ~CoGuard() { if (active) CoUninitialize(); }
    } coGuard{uninitialize};

    IUIAutomation* rawAutomation = nullptr;
    HRESULT automationHr = CoCreateInstance(
        CLSID_CUIAutomation,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&rawAutomation)
    );
    ComPtr<IUIAutomation> automation(rawAutomation);
    if (FAILED(automationHr) || !automation) {
        return {false, "UIA_INITIALIZE_FAILED"};
    }

    HWND window = nullptr;
    std::string error;
    if (!findUniqueWindow(widen(options.windowTitle), window, error)) {
        return {false, std::move(error)};
    }
    ComPtr<IUIAutomationElement> root;
    if (FAILED(automation->ElementFromHandle(window, root.put())) || !root) {
        return {false, "PILOTUI_ROOT_FAILED"};
    }

    ComPtr<IUIAutomationElement> composer;
    ComPtr<IUIAutomationValuePattern> composerPattern;
    RECT composerRect{};
    if (!findComposer(
            automation.get(), root.get(), widen(options.composerName),
            composer, composerPattern, composerRect, error)) {
        if (options.debug) {
            logComposerDiagnostics(automation.get(), root.get(), callbacks);
        }
        return {false, std::move(error)};
    }
    ComPtr<IUIAutomationElement> sendButton;
    ComPtr<IUIAutomationInvokePattern> invokePattern;
    if (!findSendButton(
            automation.get(), root.get(), widen(options.sendButtonName), composerRect,
            sendButton, invokePattern, error)) {
        return {false, std::move(error)};
    }

    std::wstring composerValue;
    if (!currentComposerValue(composerPattern.get(), composerValue)) {
        return {false, "COMPOSER_READ_FAILED"};
    }
    HistoryScan history = scanHistory(
        automation.get(), root.get(), composer.get(), composerRect,
        L"__YAL_AUTOUNICOMHELPER_DISCOVERY_SENTINEL__"
    );
    if (!history.readable || history.surfaces == 0) {
        return {false, "ACTIVE_HISTORY_UNAVAILABLE"};
    }

    RECT buttonRect{};
    getRect(sendButton.get(), buttonRect);
    const bool composerEmpty = simplifyWide(composerValue).empty();
    std::string detail = "DISCOVERY_OK history_surfaces=" + std::to_string(history.surfaces) +
        " composer_empty=" + std::to_string(composerEmpty ? 1 : 0) +
        " composer_rect=" + std::to_string(composerRect.left) + "," +
        std::to_string(composerRect.top) + "," + std::to_string(composerRect.right) + "," +
        std::to_string(composerRect.bottom) +
        " send_rect=" + std::to_string(buttonRect.left) + "," +
        std::to_string(buttonRect.top) + "," + std::to_string(buttonRect.right) + "," +
        std::to_string(buttonRect.bottom);
    log(callbacks, "Auto UNICOM UIA: " + detail);
    return {true, std::move(detail), history.surfaces, composerEmpty};
}

#endif

} // namespace

Result submitWithComposerAmbiguityRetry(
    const SubmitAttempt& submitAttempt,
    const RetryWait& retryWait,
    const Callbacks& callbacks
) {
    if (!submitAttempt) {
        return {Status::FailedBeforeSubmit, "SUBMIT_ATTEMPT_MISSING"};
    }

    Result result = submitAttempt();
    if (result.status != Status::FailedBeforeSubmit ||
        result.detail != "COMPOSER_AMBIGUOUS") {
        return result;
    }

    log(callbacks,
        "Auto UNICOM UIA: composer ambiguous before submit; retrying once after " +
        std::to_string(kComposerAmbiguousRetryDelayMs) + " ms");
    if (shouldStop(callbacks) || !retryWait ||
        !retryWait(kComposerAmbiguousRetryDelayMs) || shouldStop(callbacks)) {
        return {Status::Cancelled, "CANCELLED_BEFORE_COMPOSER_RETRY"};
    }
    return submitAttempt();
}

DiscoveryResult discoverActiveFrequencyControls(const Options& options, const Callbacks& callbacks) {
#if IBM
    return discoverWindows(options, callbacks);
#else
    (void)options;
    (void)callbacks;
    return {false, "PLATFORM_UNSUPPORTED"};
#endif
}

Result submitActiveFrequencyMessage(const Options& options, const Callbacks& callbacks) {
    if (options.message.empty()) {
        return {Status::FailedBeforeSubmit, "MESSAGE_EMPTY"};
    }
#if IBM
    return submitWithComposerAmbiguityRetry(
        [&]() { return submitWindows(options, callbacks); },
        [&](int delayMs) { return waitForComposerRetry(callbacks, delayMs); },
        callbacks
    );
#else
    (void)callbacks;
    return {Status::Unsupported, "PLATFORM_UNSUPPORTED"};
#endif
}

bool hasOwnedComposerDraft() {
    std::lock_guard<std::mutex> lock(g_composerOwnershipMutex);
    return g_composerOwnership.active();
}

bool ownedComposerRecoveryDue(int staleMs) {
    std::lock_guard<std::mutex> lock(g_composerOwnershipMutex);
    return g_composerOwnership.recoveryDue(steadyNowMs(), std::max(0, staleMs));
}

RecoveryResult recoverOwnedComposerDraft(
    const Options& options,
    const Callbacks& callbacks
) {
    if (!hasOwnedComposerDraft()) {
        return {RecoveryStatus::None, "NO_OWNED_COMPOSER"};
    }
#if IBM
    return recoverWindows(options, callbacks);
#else
    (void)options;
    (void)callbacks;
    return {RecoveryStatus::Unsupported, "PLATFORM_UNSUPPORTED"};
#endif
}

} // namespace pilotui_message
