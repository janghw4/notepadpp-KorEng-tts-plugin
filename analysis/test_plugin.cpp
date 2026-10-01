// SPDX-License-Identifier: GPL-3.0-or-later
// Compile the production translation unit into a host with synthetic editor windows.
#include "selection_tts.cpp"
#include <filesystem>
#include <iostream>
#include <cstring>

namespace {
std::string selected_bytes;
UINT selected_code_page = CP_UTF8;
bool legacy_selection_length = false;
int selected_view = 0;
int full_document_reads = 0;
HWND last_editor = nullptr;
std::wstring test_directory;
std::wstring last_status;
int assertions = 0;
int audio_output_index = -1;

void require(bool condition, const char* message) {
    ++assertions;
    if (!condition) throw std::runtime_error(message);
}
LRESULT CALLBACK host_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case NPPM_GETCURRENTSCINTILLA: *reinterpret_cast<int*>(lparam) = selected_view; return TRUE;
    case NPPM_GETPLUGINSCONFIGDIR:
        wcsncpy_s(reinterpret_cast<wchar_t*>(lparam), static_cast<size_t>(wparam), test_directory.c_str(), _TRUNCATE);
        return TRUE;
    case NPPM_SETSTATUSBAR: last_status = reinterpret_cast<const wchar_t*>(lparam); return TRUE;
    case SCI_GETCODEPAGE: return selected_code_page;
    case SCI_GETSELTEXT:
        last_editor = hwnd;
        if (lparam) std::memcpy(reinterpret_cast<void*>(lparam), selected_bytes.c_str(), selected_bytes.size() + 1);
        return static_cast<LRESULT>(selected_bytes.size() + (legacy_selection_length ? 1 : 0));
    case SCI_GETTEXT: case SCI_GETTEXTLENGTH: ++full_document_reads; return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

ComPtr<ISpStream> wave_output(const std::wstring& path) {
    ComPtr<ISpStream> stream;
    check(CoCreateInstance(CLSID_SpStream, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(stream.GetAddressOf())), "Create test audio stream");
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = 22050;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 2;
    format.nAvgBytesPerSec = 44100;
    check(stream->BindToFile(path.c_str(), SPFM_CREATE_ALWAYS, &SPDFID_WaveFormatEx, &format, 0), "Create test WAV");
    return stream;
}
std::wstring current_voice_language() {
    ComPtr<ISpObjectToken> token;
    check(speech->voice()->GetVoice(token.GetAddressOf()), "Inspect selected voice");
    ComPtr<ISpDataKey> attributes;
    check(token->OpenKey(L"Attributes", attributes.GetAddressOf()), "Inspect voice attributes");
    wchar_t* value = nullptr;
    check(attributes->GetStringValue(L"Language", &value), "Inspect voice language");
    std::wstring result(value);
    CoTaskMemFree(value);
    return result;
}
void render_command(const wchar_t* filename, const std::string& text, UINT code_page, void(*command)(), const wchar_t* expected_language) {
    selected_bytes = text;
    selected_code_page = code_page;
    const auto path = test_directory + L"\\" + filename;
    auto stream = wave_output(path);
    speech = std::make_unique<SpeechEngine>();
    check(speech->voice()->SetOutput(stream.Get(), TRUE), "Route speech to test WAV");
    command();
    require(speech->voice()->WaitUntilDone(15000) == S_OK, "Speech did not finish");
    if (expected_language) require(current_voice_language().find(expected_language) != std::wstring::npos, "Incorrect language voice selected");
    speech.reset();
    check(stream->Close(), "Close test WAV");
    require(std::filesystem::file_size(path) > 4000, "Speech produced no useful audio");
}
void test_voice_switching() {
    // File sinks do not deliver voice events here. Use a muted audio-device sink.
    speech = std::make_unique<SpeechEngine>();
    if (audio_output_index >= 0) {
        ComPtr<ISpObjectTokenCategory> category;
        check(CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(category.GetAddressOf())), "Open test audio outputs");
        check(category->SetId(SPCAT_AUDIOOUT, FALSE), "Open audio output category");
        ComPtr<IEnumSpObjectTokens> outputs;
        check(category->EnumTokens(nullptr, nullptr, outputs.GetAddressOf()), "Enumerate test audio outputs");
        ComPtr<ISpObjectToken> token;
        check(outputs->Item(static_cast<ULONG>(audio_output_index), token.GetAddressOf()), "Select test audio output");
        check(speech->voice()->SetOutput(token.Get(), TRUE), "Use explicit test audio output");
    }
    check(speech->voice()->SetVolume(0), "Mute the device-level voice test");
    speech->set_rate(5);
    check(speech->voice()->SetNotifyWin32Event(), "Enable voice event delivery");
    check(speech->voice()->SetInterest(SPFEI(SPEI_VOICE_CHANGE), SPFEI(SPEI_VOICE_CHANGE)), "Observe voice switching");
    speech->speak(L"안녕하세요. Hello, world. 감사합니다.", Language::Auto);
    require(speech->voice()->WaitUntilDone(15000) == S_OK, "Muted voice test did not finish");
    std::vector<std::wstring> voice_changes;
    SPEVENT event{};
    while (true) {
        ULONG fetched = 0;
        check(speech->voice()->GetEvents(1, &event, &fetched), "Read voice-change events");
        if (!fetched) break;
        if (event.eEventId == SPEI_VOICE_CHANGE && event.elParamType == SPET_LPARAM_IS_TOKEN) {
            auto token = reinterpret_cast<ISpObjectToken*>(event.lParam);
            ComPtr<ISpDataKey> attributes;
            check(token->OpenKey(L"Attributes", attributes.GetAddressOf()), "Inspect changed voice");
            wchar_t* value = nullptr;
            check(attributes->GetStringValue(L"Language", &value), "Inspect changed voice language");
            voice_changes.emplace_back(value);
            CoTaskMemFree(value);
        }
        if (event.elParamType == SPET_LPARAM_IS_TOKEN || event.elParamType == SPET_LPARAM_IS_OBJECT)
            reinterpret_cast<IUnknown*>(event.lParam)->Release();
        else if (event.elParamType == SPET_LPARAM_IS_POINTER || event.elParamType == SPET_LPARAM_IS_STRING)
            CoTaskMemFree(reinterpret_cast<void*>(event.lParam));
        event = {};
    }
    std::wcout << L"Mixed voice events:";
    for (const auto& language : voice_changes) std::wcout << L" " << language;
    std::wcout << L"\n";
    voice_changes.erase(std::unique(voice_changes.begin(), voice_changes.end()), voice_changes.end());
    require(voice_changes.size() == 3, "Expected three voice changes in Korean-English-Korean text");
    require(voice_changes[0] == L"412" && voice_changes[1] == L"409" && voice_changes[2] == L"412",
            "Bilingual synthesis did not actually switch Korean-English-Korean voices");
    speech.reset();
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc < 2 || argc > 4) throw std::runtime_error("Pass an empty output directory, optional audio output index, and --core-only or --full.");
        test_directory = argv[1];
        if (argc >= 3) audio_output_index = _wtoi(argv[2]);
        const bool core_only = argc == 4 && std::wstring(argv[3]) == L"--core-only";
        if (argc == 4 && !core_only && std::wstring(argv[3]) != L"--full") throw std::runtime_error("Unknown test mode.");
        std::filesystem::create_directories(test_directory);
        WNDCLASSW wc{};
        wc.lpfnWndProc = host_proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"KorEngTtsSyntheticEditor";
        require(RegisterClassW(&wc) != 0, "Cannot register test window");
        HWND main = CreateWindowW(wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
        HWND first = CreateWindowW(wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
        HWND second = CreateWindowW(wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
        const auto legacy_path = test_directory + L"\\SelectionTTS.ini";
        config_path = legacy_path;
        const auto legacy_filters = make_speech_filters(false, L"#\\d+\n주석\n  spaced  \n\"quoted\"");
        save_speech_filters(legacy_filters);
        if (!WritePrivateProfileStringW(L"Speech", L"Rate", L"3", legacy_path.c_str())) throw std::runtime_error("Cannot create legacy test settings.");
        config_path.clear();
        setInfo({main, first, second});
        require(std::wstring(getName()) == L"KorEng TTS", "The public plugin name was not updated");
        require(config_path == test_directory + L"\\KorEngTTS.ini", "The new settings filename is incorrect");
        require(current_rate == 3 && !filters.enabled && filters.patterns == legacy_filters.patterns, "Legacy speed and Unicode regex settings did not migrate");
        require(std::filesystem::exists(legacy_path), "Migration removed the original settings");
        filters = make_speech_filters(true, default_filter_regex);
        save_speech_filters(filters);
        require(com_ready, "COM did not initialize");
        require(isUnicode() == TRUE, "Plugin is not Unicode");
        int count = 0;
        auto items = getFuncsArray(&count);
        require(count == 14 && items[0]._pFunc, "Plugin command ABI is invalid");

        selected_bytes = u8"Selected English. 선택한 한국어.";
        require(selection() == L"Selected English. 선택한 한국어.", "UTF-8 selection mismatch");
        require(last_editor == first, "Main view was not read");
        selected_view = 1;
        require(selection() == L"Selected English. 선택한 한국어." && last_editor == second, "Split view was not read");
        selected_bytes.clear();
        require(selection().empty(), "Empty selection must remain empty");
        selected_bytes = "A";
        require(selection() == L"A", "A one-byte selection was lost");
        selected_bytes = u8"한";
        require(selection() == L"한", "The final Korean character was truncated");
        legacy_selection_length = true;
        require(selection() == L"한", "Legacy selection size contract failed");
        selected_bytes.clear();
        require(selection().empty(), "Legacy empty selection failed");
        legacy_selection_length = false;
        selected_bytes = "one\r\nthree";
        require(selection() == L"one\r\nthree", "Multiselection line breaks changed");
        selected_bytes = std::string("A\0B", 3);
        require(selection() == L"A B", "Embedded NUL truncated selection");
        selected_bytes = std::string(2 * 1024 * 1024 + 1, 'x');
        bool oversized = false;
        try { selection(); } catch (...) { oversized = true; }
        require(oversized, "Oversized selection was not rejected");
        bool invalid = false;
        try { decode_selection("\xff", CP_UTF8); } catch (...) { invalid = true; }
        require(invalid, "Invalid UTF-8 was not rejected");
        require(decode_selection(std::string("\xc7\xd1\xb1\xdb", 4), 949) == L"한글", "Korean code page 949 mismatch");

        require(prepare_speech_text(L"**bold** *italic*", filters) == L"bold italic", "Asterisks were not skipped");
        require(prepare_speech_text(L"문장[1]. English[123].", filters) == L"문장. English.", "Numeric references were not skipped");
        require(prepare_speech_text(L"one[12]two", filters) == L"one two", "Skipping a reference joined words");
        require(prepare_speech_text(L"[1][22]한글***", filters) == L"한글", "Adjacent markers were not skipped");
        require(prepare_speech_text(L"[name] [] [1,2] [12", filters) == L"[name] [] [1,2] [12", "Non-numeric brackets were changed");
        require(prepare_speech_text(L"***[123]**", filters).empty(), "Markers-only text did not become empty");
        require(prepare_speech_text(L"Numbers 42 and [001]", filters) == L"Numbers 42 and ", "Ordinary numbers were changed");
        require(prepare_speech_text(L"첫 줄\n*둘째 줄*[2]", filters) == L"첫 줄\n둘째 줄", "Filtering changed the line structure");
        require(prepare_speech_text(L"A[1]*", make_speech_filters(false, default_filter_regex)) == L"A[1]*", "Disabled filters changed text");
        require(prepare_speech_text(L"A[1]*", make_speech_filters(true, L"")) == L"A[1]*", "Empty filter list changed text");
        require(prepare_speech_text(L"Text#123!", make_speech_filters(true, L"#\\d+")) == L"Text!", "Custom filter did not apply");
        require(prepare_speech_text(L"한글 주석 한글", make_speech_filters(true, L"주석")) == L"한글  한글", "Korean regex did not apply");
        require(prepare_speech_text(L"Hello", make_speech_filters(true, L"^|$")) == L"Hello", "Zero-length matches changed text or stalled");
        bool bad_regex = false;
        try { make_speech_filters(true, L"\\*\n["); }
        catch (const std::exception& error) { bad_regex = std::string(error.what()).find("line 2") != std::string::npos; }
        require(bad_regex, "Invalid regex did not identify its line");

        auto mixed = split_languages(L"안녕하세요. Hello, world! 다시 읽어요.");
        require(mixed.size() == 3, "Mixed-language run detection failed");
        require(mixed[0].language == Language::Korean && mixed[1].language == Language::English && mixed[2].language == Language::Korean, "Mixed language order failed");
        std::wstring joined;
        for (const auto& segment : mixed) joined += segment.text;
        require(joined == L"안녕하세요. Hello, world! 다시 읽어요.", "Language detection changed text");
        require(split_languages(L"English 한국어", Language::English).size() == 1, "Forced language was not honored");
        require(split_languages(L"123").front().language == Language::English, "Numbers-only default failed");
        require(letter_language(L'ᄀ') == Language::Korean && letter_language(L'ㄱ') == Language::Korean, "Hangul jamo detection failed");
        require(escape_xml(L"<voice a='x'>&\"") == L"&lt;voice a=&apos;x&apos;&gt;&amp;&quot;", "XML escaping failed");
        require(speech_xml(split_languages(L"<silence msec='99999'/> 한글")).find(L"<silence") == std::wstring::npos, "Input could inject speech markup");

        selected_bytes = "***[12]*";
        speak_auto();
        require(!speech && last_status == L"KorEng TTS: nothing to read after skipping symbols", "Markers-only selection did not stop quietly");
        ComPtr<ISpStream> stream;
        if (!core_only) {
            std::wcout << L"English voice: " << voice_name(Language::English) << L"\n";
            std::wcout << L"Korean voice: " << voice_name(Language::Korean) << L"\n";
            render_command(L"english.wav", "This is the selected English sentence.", CP_UTF8, speak_auto, L"409");
            render_command(L"korean.wav", u8"선택한 한국어 문장을 읽습니다.", CP_UTF8, speak_auto, L"412");
            render_command(L"mixed.wav", u8"안녕하세요. This sentence is English. 다시 한국어입니다.", CP_UTF8, speak_auto, nullptr);
            render_command(L"forced_english.wav", "This uses the English voice.", CP_UTF8, speak_english, L"409");
            render_command(L"forced_korean.wav", u8"한국어 음성을 직접 선택했습니다.", CP_UTF8, speak_korean, L"412");
            render_command(L"korean_ansi.wav", std::string("\xc7\xd1\xb1\xdb", 4), 949, speak_korean, L"412");
            render_command(L"literal_markup.wav", u8"<voice required='Language=412'>Hello</voice> 한국어", CP_UTF8, speak_auto, nullptr);
            render_command(L"filtered_english.wav", "**This is the selected English sentence.**[123]", CP_UTF8, speak_auto, L"409");
            render_command(L"filtered_korean.wav", u8"**선택한 한국어 문장을 읽습니다.**[12]", CP_UTF8, speak_auto, L"412");
            test_voice_switching();

            stream = wave_output(test_directory + L"\\controls.wav");
            speech = std::make_unique<SpeechEngine>();
            check(speech->voice()->SetOutput(stream.Get(), TRUE), "Route controls test audio");
            speech->speak(L"Test pause, resume, and stop.", Language::English);
            pause_resume(); require(speech->paused(), "Pause failed");
            pause_resume(); require(!speech->paused(), "Resume failed");
            pause_resume(); stop(); require(!speech->paused(), "Stop did not clear paused state");
            require(speech->voice()->WaitUntilDone(3000) == S_OK, "Stop did not purge pending speech");
            change_rate(2); require(speech->rate() == 2, "Speed command failed");
            require(GetPrivateProfileIntW(L"Speech", L"Rate", 0, config_path.c_str()) == 2, "Speed was not saved");
            apply_rate(5, false);
            LONG actual_rate = 0;
            check(speech->voice()->GetRate(&actual_rate), "Inspect preview speech rate");
            require(actual_rate == 5, "Speed preview did not reach the speech engine");
            require(GetPrivateProfileIntW(L"Speech", L"Rate", 0, config_path.c_str()) == 2, "Speed preview changed saved rate");
            normal_speed(); require(speech->rate() == 0, "Normal speed command failed");
            change_rate(200); require(speech->rate() == 10, "Maximum speed bound failed");
            change_rate(-200); require(speech->rate() == -10, "Minimum speed bound failed");
        } else {
            change_rate(2);
            require(current_rate == 2 && GetPrivateProfileIntW(L"Speech", L"Rate", 0, config_path.c_str()) == 2, "Speed was not saved without a voice");
            apply_rate(5, false);
            require(current_rate == 5 && GetPrivateProfileIntW(L"Speech", L"Rate", 0, config_path.c_str()) == 2, "Speed preview changed the saved rate without a voice");
            normal_speed(); require(current_rate == 0, "Normal speed failed without a voice");
            change_rate(200); require(current_rate == 10, "Maximum speed bound failed without a voice");
            change_rate(-200); require(current_rate == -10, "Minimum speed bound failed without a voice");
        }
        auto custom = make_speech_filters(false, L"#\\d+\n주석\n  spaced  \n\"quoted\"");
        save_speech_filters(custom);
        auto reloaded = load_speech_filters();
        require(!reloaded.enabled && reloaded.patterns == custom.patterns, "Saved regex settings did not reload");
        require(GetPrivateProfileIntW(L"Speech", L"Rate", 0, config_path.c_str()) == -10, "Saving filters changed speech speed");
        save_speech_filters(make_speech_filters(true, L""));
        require(load_speech_filters().expressions.empty(), "Saving no filters did not clear the active list");
        SCNotification shutdown{};
        shutdown.nmhdr.code = NPPN_SHUTDOWN;
        beNotified(&shutdown);
        require(!speech && !com_ready, "Shutdown did not release speech and COM");
        if (stream) check(stream->Close(), "Close controls WAV");
        stream.Reset();
        setInfo({main, first, second});
        require(current_rate == -10 && filters.expressions.empty(), "Existing KorEng TTS settings were replaced with legacy values");
        require(GetPrivateProfileIntW(L"Speech", L"Rate", 0, legacy_path.c_str()) == 3, "Saving new settings changed the legacy file");
        beNotified(&shutdown);
        require(full_document_reads == 0, "Plugin attempted to read the complete document");
        DestroyWindow(first); DestroyWindow(second); DestroyWindow(main);
        std::cout << "PASS: " << assertions << " checks; "
                  << (core_only ? "core only, no speech synthesis" : "nine speech WAV fixtures")
                  << "; no full-document reads.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        cleanup();
        return 1;
    }
}
