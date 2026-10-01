// SPDX-License-Identifier: GPL-3.0-or-later
// Independent implementation; Notepad++ ABI headers are in ../reference.
#include "PluginInterface.h"
#include "speech_core.hpp"
#include "resource.h"
#include <commctrl.h>
#include <memory>
#include <functional>

using namespace selection_tts;
namespace {
constexpr wchar_t plugin_name[] = L"Selection TTS";
NppData npp{};
std::unique_ptr<SpeechEngine> speech;
bool com_owned = false;
bool com_ready = false;
std::wstring config_path;
int current_rate = 0;
HMODULE module_handle = nullptr;
SpeechFilters filters;

void notice(const std::wstring& text, UINT icon = MB_ICONINFORMATION) {
    MessageBoxW(npp._nppHandle, text.c_str(), plugin_name, MB_OK | icon);
}
void status(const std::wstring& text) {
    SendMessageW(npp._nppHandle, NPPM_SETSTATUSBAR, STATUSBAR_DOC_TYPE, reinterpret_cast<LPARAM>(text.c_str()));
}
template<typename F> void guarded(F action) noexcept {
    try { action(); }
    catch (const std::exception& e) {
        const std::string message = e.what();
        notice(std::wstring(message.begin(), message.end()), MB_ICONERROR);
    } catch (...) { notice(L"Speech failed unexpectedly. The document has not been changed.", MB_ICONERROR); }
}
SpeechEngine& engine() {
    if (!com_ready) throw std::runtime_error("Windows speech could not initialize in this editor session.");
    if (!speech) { speech = std::make_unique<SpeechEngine>(); speech->set_rate(current_rate); }
    return *speech;
}

SpeechFilters load_speech_filters() {
    if (config_path.empty()) return make_speech_filters(true, default_filter_regex);
    const int count = static_cast<int>(GetPrivateProfileIntW(L"Filters", L"Count", -1, config_path.c_str()));
    if (count == -1) return make_speech_filters(true, default_filter_regex);
    if (count < 0 || count > 64) throw std::runtime_error("The saved filter count is invalid. Default filters are active.");
    std::wstring patterns;
    for (int i = 1; i <= count; ++i) {
        std::wstring pattern(65537, L'\0');
        const DWORD length = GetPrivateProfileStringW(L"Filters", (L"Pattern" + std::to_wstring(i) + L"Utf16").c_str(),
            L"", pattern.data(), static_cast<DWORD>(pattern.size()), config_path.c_str());
        pattern.resize(length);
        if (i != 1) patterns += L'\n';
        patterns += decode_filter_pattern(pattern);
    }
    const bool enabled = GetPrivateProfileIntW(L"Filters", L"Enabled", 1, config_path.c_str()) != 0;
    return make_speech_filters(enabled, patterns);
}

void save_speech_filters(const SpeechFilters& candidate) {
    if (config_path.empty()) throw std::runtime_error("The plugin settings directory is unavailable.");
    const auto write = [&](const std::wstring& key, const std::wstring& value) {
        if (!WritePrivateProfileStringW(L"Filters", key.c_str(), value.c_str(), config_path.c_str()))
            throw std::runtime_error("The text filters could not be saved.");
    };
    for (size_t i = 0; i < candidate.patterns.size(); ++i)
        write(L"Pattern" + std::to_wstring(i + 1) + L"Utf16", encode_filter_pattern(candidate.patterns[i]));
    write(L"Count", std::to_wstring(candidate.patterns.size()));
    write(L"Enabled", candidate.enabled ? L"1" : L"0");
}

INT_PTR CALLBACK filters_dialog_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM) noexcept {
    try {
        if (message == WM_INITDIALOG) {
            CheckDlgButton(dialog, IDC_FILTER_ENABLE, filters.enabled ? BST_CHECKED : BST_UNCHECKED);
            SetDlgItemTextW(dialog, IDC_FILTER_REGEX, filters.pattern_text.c_str());
            SendDlgItemMessageW(dialog, IDC_FILTER_REGEX, EM_SETLIMITTEXT, 16384, 0);
            return TRUE;
        }
        if (message == WM_COMMAND) {
            switch (LOWORD(wparam)) {
            case IDC_FILTER_DEFAULTS:
                CheckDlgButton(dialog, IDC_FILTER_ENABLE, BST_CHECKED);
                SetDlgItemTextW(dialog, IDC_FILTER_REGEX, default_filter_regex);
                return TRUE;
            case IDOK: {
                const int count = GetWindowTextLengthW(GetDlgItem(dialog, IDC_FILTER_REGEX));
                std::wstring text(static_cast<size_t>(count) + 1, L'\0');
                const int copied = GetDlgItemTextW(dialog, IDC_FILTER_REGEX, text.data(), count + 1);
                text.resize(static_cast<size_t>(copied));
                auto candidate = make_speech_filters(IsDlgButtonChecked(dialog, IDC_FILTER_ENABLE) == BST_CHECKED, text);
                save_speech_filters(candidate);
                filters = std::move(candidate);
                status(filters.enabled ? L"Selection TTS: text filters saved" : L"Selection TTS: text filters disabled");
                EndDialog(dialog, IDOK);
                return TRUE;
            }
            case IDCANCEL: EndDialog(dialog, IDCANCEL); return TRUE;
            }
        }
    } catch (const std::exception& error) {
        const std::string message_text = error.what();
        const std::wstring text(message_text.begin(), message_text.end());
        MessageBoxW(dialog, text.c_str(), plugin_name, MB_OK | MB_ICONERROR);
        return TRUE;
    } catch (...) {
        MessageBoxW(dialog, L"The text filters could not be applied.", plugin_name, MB_OK | MB_ICONERROR);
        return TRUE;
    }
    return FALSE;
}

void edit_text_filters() {
    guarded([] {
        if (DialogBoxParamW(module_handle, MAKEINTRESOURCEW(IDD_TEXT_FILTERS), npp._nppHandle, filters_dialog_proc, 0) == -1)
            throw std::runtime_error("The text filter settings window could not be opened.");
    });
}

std::wstring selection() {
    int active = -1;
    SendMessageW(npp._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, reinterpret_cast<LPARAM>(&active));
    HWND editor = active == 0 ? npp._scintillaMainHandle : active == 1 ? npp._scintillaSecondHandle : nullptr;
    if (!editor) throw std::runtime_error("The active editor is unavailable.");
    const LRESULT bytes = SendMessageW(editor, SCI_GETSELTEXT, 0, 0);
    if (bytes <= 0) return {};
    if (bytes > 2 * 1024 * 1024 + 1) throw std::runtime_error("Select at most 2 MiB of text at a time.");
    // Scintilla 5 returns the byte count without NUL; older versions included it.
    // A sentinel distinguishes both contracts without truncating embedded NULs.
    std::string buffer(static_cast<size_t>(bytes) + 1, '\x01');
    SendMessageW(editor, SCI_GETSELTEXT, 0, reinterpret_cast<LPARAM>(buffer.data()));
    const size_t text_size = buffer[static_cast<size_t>(bytes)] == '\0'
        ? static_cast<size_t>(bytes) : static_cast<size_t>(bytes - 1);
    buffer.resize(text_size);
    const UINT cp = static_cast<UINT>(SendMessageW(editor, SCI_GETCODEPAGE, 0, 0));
    return decode_selection(buffer, cp);
}
void speak_selection(Language language) {
    guarded([&] {
        auto text = selection();
        if (text.find_first_not_of(L" \t\r\n") == std::wstring::npos) {
            notice(L"Select the text you want to hear first.\n\n\uC77D\uC744 \uD14D\uC2A4\uD2B8\uB97C \uBA3C\uC800 \uC120\uD0DD\uD574 \uC8FC\uC138\uC694.");
            return;
        }
        text = prepare_speech_text(text, filters);
        if (text.find_first_not_of(L" \t\r\n") == std::wstring::npos) {
            if (speech) speech->stop();
            status(L"Selection TTS: nothing to read after skipping symbols");
            return;
        }
        engine().speak(text, language);
        status(L"Selection TTS: speaking selection");
    });
}
void speak_auto() { speak_selection(Language::Auto); }
void speak_english() { speak_selection(Language::English); }
void speak_korean() { speak_selection(Language::Korean); }
void stop() { guarded([] { if (speech) speech->stop(); status(L"Selection TTS: stopped"); }); }
void pause_resume() { guarded([] { if (speech) { speech->toggle_pause(); status(speech->paused() ? L"Selection TTS: paused" : L"Selection TTS: resumed"); } }); }
void apply_rate(int value, bool save) {
    current_rate = std::clamp(value, -10, 10);
    if (speech) speech->set_rate(current_rate);
    if (save && !config_path.empty() && !WritePrivateProfileStringW(L"Speech", L"Rate", std::to_wstring(current_rate).c_str(), config_path.c_str()))
        throw std::runtime_error("Speed changed for this session, but the settings file could not be saved.");
    status(L"Selection TTS: speed " + std::to_wstring(current_rate) + L" (-10 to 10)");
}
void change_rate(int value) { guarded([&] { apply_rate(value, true); }); }
void slower() { change_rate(current_rate - 1); }
void faster() { change_rate(current_rate + 1); }
void normal_speed() { change_rate(0); }

void show_speed_value(HWND dialog) {
    const std::wstring value = L"Speed: " + std::to_wstring(current_rate) +
        (current_rate == 0 ? L" (normal)" : current_rate < 0 ? L" (slower)" : L" (faster)");
    SetDlgItemTextW(dialog, IDC_SPEED_VALUE, value.c_str());
}

INT_PTR CALLBACK speed_dialog_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    try {
        if (message == WM_INITDIALOG) {
            SetWindowLongPtrW(dialog, DWLP_USER, lparam);
            SendDlgItemMessageW(dialog, IDC_SPEED_SLIDER, TBM_SETRANGEMIN, FALSE, -10);
            SendDlgItemMessageW(dialog, IDC_SPEED_SLIDER, TBM_SETRANGEMAX, FALSE, 10);
            SendDlgItemMessageW(dialog, IDC_SPEED_SLIDER, TBM_SETTICFREQ, 1, 0);
            SendDlgItemMessageW(dialog, IDC_SPEED_SLIDER, TBM_SETPAGESIZE, 0, 2);
            SendDlgItemMessageW(dialog, IDC_SPEED_SLIDER, TBM_SETPOS, TRUE, current_rate);
            show_speed_value(dialog);
            return TRUE;
        }
        if (message == WM_HSCROLL && reinterpret_cast<HWND>(lparam) == GetDlgItem(dialog, IDC_SPEED_SLIDER)) {
            apply_rate(static_cast<int>(SendDlgItemMessageW(dialog, IDC_SPEED_SLIDER, TBM_GETPOS, 0, 0)), false);
            show_speed_value(dialog);
            return TRUE;
        }
        if (message == WM_COMMAND) {
            switch (LOWORD(wparam)) {
            case IDC_SPEED_NORMAL:
                SendDlgItemMessageW(dialog, IDC_SPEED_SLIDER, TBM_SETPOS, TRUE, 0);
                apply_rate(0, false);
                show_speed_value(dialog);
                return TRUE;
            case IDOK:
                apply_rate(current_rate, true);
                EndDialog(dialog, IDOK);
                return TRUE;
            case IDCANCEL:
                apply_rate(static_cast<int>(GetWindowLongPtrW(dialog, DWLP_USER)), false);
                EndDialog(dialog, IDCANCEL);
                return TRUE;
            }
        }
        if (message == WM_CLOSE) {
            apply_rate(static_cast<int>(GetWindowLongPtrW(dialog, DWLP_USER)), false);
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
    } catch (const std::exception& error) {
        const std::string message_text = error.what();
        const std::wstring text(message_text.begin(), message_text.end());
        MessageBoxW(dialog, text.c_str(), plugin_name, MB_OK | MB_ICONERROR);
        return TRUE;
    } catch (...) {
        MessageBoxW(dialog, L"The speech speed could not be changed.", plugin_name, MB_OK | MB_ICONERROR);
        return TRUE;
    }
    return FALSE;
}

void edit_speech_speed() {
    guarded([] {
        INITCOMMONCONTROLSEX controls{sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES};
        if (!InitCommonControlsEx(&controls)) throw std::runtime_error("The speed slider could not be initialized.");
        if (DialogBoxParamW(module_handle, MAKEINTRESOURCEW(IDD_SPEECH_SPEED), npp._nppHandle,
            speed_dialog_proc, static_cast<LPARAM>(current_rate)) == -1)
            throw std::runtime_error("The speech speed window could not be opened.");
    });
}
void about() {
    guarded([] {
        std::wstring info = L"Selection TTS 1.3\n\nSelect text, then press Ctrl+Alt+T.\nAuto switches between English and Korean letter runs.\nAdjust speed in Speech speed (slider).\nConfigure skipped text in Text filters (regex).\n\nPause / resume: Ctrl+Alt+P\nStop: Ctrl+Alt+Shift+T\n\nWindows SAPI voices (offline):\n";
        for (auto language : {Language::English, Language::Korean}) {
            try { info += voice_name(language) + L"\n"; }
            catch (...) { info += language == Language::Korean ? L"Korean: not installed\n" : L"English (US): not installed\n"; }
        }
        info += L"\nSpeed: " + std::to_wstring(current_rate) + L"\n\nSelected text is neither saved nor sent over a network.";
        notice(info);
    });
}
void cleanup() noexcept {
    if (speech) { try { speech->stop(); } catch (...) {} speech.reset(); }
    if (com_owned) CoUninitialize();
    com_owned = false; com_ready = false;
}

ShortcutKey read_key{true, true, false, 'T'};
ShortcutKey pause_key{true, true, false, 'P'};
ShortcutKey stop_key{true, true, true, 'T'};
FuncItem commands[] = {
    {L"Read selection (Auto EN / KO)", speak_auto, 0, false, &read_key},
    {L"Read selection in English", speak_english, 0, false, nullptr},
    {L"Read selection in Korean", speak_korean, 0, false, nullptr},
    {L"", nullptr, 0, false, nullptr},
    {L"Pause / Resume", pause_resume, 0, false, &pause_key},
    {L"Stop", stop, 0, false, &stop_key},
    {L"", nullptr, 0, false, nullptr},
    {L"Slower", slower, 0, false, nullptr},
    {L"Faster", faster, 0, false, nullptr},
    {L"Normal speed", normal_speed, 0, false, nullptr},
    {L"Speech speed (slider)...", edit_speech_speed, 0, false, nullptr},
    {L"Text filters (regex)...", edit_text_filters, 0, false, nullptr},
    {L"", nullptr, 0, false, nullptr},
    {L"Voices and help", about, 0, false, nullptr}
};
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) module_handle = module;
    return TRUE;
}
extern "C" __declspec(dllexport) const TCHAR* getName() { return plugin_name; }
extern "C" __declspec(dllexport) BOOL isUnicode() { return TRUE; }
extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* count) {
    if (count) *count = static_cast<int>(std::size(commands));
    return commands;
}
extern "C" __declspec(dllexport) void setInfo(NppData data) {
    npp = data;
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    com_owned = SUCCEEDED(hr);
    com_ready = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    guarded([] {
        filters = make_speech_filters(true, default_filter_regex);
        wchar_t path[MAX_PATH]{};
        if (SendMessageW(npp._nppHandle, NPPM_GETPLUGINSCONFIGDIR, MAX_PATH, reinterpret_cast<LPARAM>(path)) && path[0]) {
            config_path = std::wstring(path) + L"\\SelectionTTS.ini";
            wchar_t rate[16]{};
            GetPrivateProfileStringW(L"Speech", L"Rate", L"0", rate, 16, config_path.c_str());
            current_rate = std::clamp(_wtoi(rate), -10, 10);
            filters = load_speech_filters();
        }
    });
}
extern "C" __declspec(dllexport) void beNotified(SCNotification* notification) {
    if (notification && notification->nmhdr.code == NPPN_SHUTDOWN) cleanup();
}
extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM) { return TRUE; }
