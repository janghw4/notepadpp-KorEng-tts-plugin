// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <sapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>
#include <stdexcept>
#include <sstream>
#include <regex>

namespace selection_tts {
using Microsoft::WRL::ComPtr;
enum class Language { Auto, English, Korean };
struct Segment { Language language; std::wstring text; };

inline void check(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        std::ostringstream message;
        message << operation << " (HRESULT 0x" << std::hex << static_cast<unsigned long>(hr) << ")";
        throw std::runtime_error(message.str());
    }
}

inline Language letter_language(wchar_t c) {
    if ((c >= 0xAC00 && c <= 0xD7A3) || (c >= 0x1100 && c <= 0x11FF) ||
        (c >= 0x3130 && c <= 0x318F) || (c >= 0xA960 && c <= 0xA97F) ||
        (c >= 0xD7B0 && c <= 0xD7FF) || (c >= 0x4E00 && c <= 0x9FFF)) return Language::Korean;
    if ((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') ||
        (c >= 0xC0 && c <= 0x024F)) return Language::English;
    return Language::Auto;
}

inline constexpr wchar_t default_filter_regex[] = L"\\*\n\\[\\d+\\]";
struct SpeechFilters {
    bool enabled = true;
    std::wstring pattern_text;
    std::vector<std::wstring> patterns;
    std::vector<std::wregex> expressions;
};

inline std::wstring encode_filter_pattern(std::wstring_view pattern) {
    // ASCII storage preserves Korean, quotes, and meaningful surrounding spaces in INI files.
    constexpr wchar_t hex[] = L"0123456789ABCDEF";
    std::wstring encoded;
    encoded.reserve(pattern.size() * 4);
    for (wchar_t c : pattern) {
        for (int shift = 12; shift >= 0; shift -= 4) encoded += hex[(static_cast<unsigned int>(c) >> shift) & 15];
    }
    return encoded;
}

inline std::wstring decode_filter_pattern(std::wstring_view encoded) {
    if (encoded.size() % 4 != 0) throw std::runtime_error("The saved text filter encoding is invalid.");
    std::wstring pattern;
    for (size_t i = 0; i < encoded.size(); i += 4) {
        unsigned int value = 0;
        for (size_t j = 0; j < 4; ++j) {
            const wchar_t c = encoded[i + j];
            int digit = c >= L'0' && c <= L'9' ? c - L'0' :
                c >= L'A' && c <= L'F' ? c - L'A' + 10 : c >= L'a' && c <= L'f' ? c - L'a' + 10 : -1;
            if (digit < 0) throw std::runtime_error("The saved text filter encoding is invalid.");
            value = value * 16 + static_cast<unsigned int>(digit);
        }
        pattern += static_cast<wchar_t>(value);
    }
    return pattern;
}

inline SpeechFilters make_speech_filters(bool enabled, std::wstring_view pattern_text) {
    SpeechFilters filters;
    filters.enabled = enabled;
    filters.pattern_text = pattern_text;
    size_t start = 0, line = 1;
    while (start <= pattern_text.size()) {
        size_t end = pattern_text.find(L'\n', start);
        if (end == std::wstring_view::npos) end = pattern_text.size();
        auto pattern = std::wstring(pattern_text.substr(start, end - start));
        if (!pattern.empty() && pattern.back() == L'\r') pattern.pop_back();
        if (!pattern.empty()) {
            if (filters.patterns.size() == 64) throw std::runtime_error("Use at most 64 filter expressions.");
            try { filters.expressions.emplace_back(pattern, std::regex_constants::ECMAScript | std::regex_constants::optimize); }
            catch (const std::regex_error& error) {
                throw std::runtime_error("Invalid regular expression on line " + std::to_string(line) + ": " + error.what());
            }
            filters.patterns.push_back(std::move(pattern));
        }
        if (end == pattern_text.size()) break;
        start = end + 1;
        ++line;
    }
    return filters;
}

inline std::wstring prepare_speech_text(std::wstring_view text, const SpeechFilters& filters) {
    std::wstring current(text);
    if (!filters.enabled) return current;
    const auto word_character = [](wchar_t c) {
        return letter_language(c) != Language::Auto || (c >= L'0' && c <= L'9');
    };
    for (const auto& expression : filters.expressions) {
        std::wstring result;
        result.reserve(current.size());
        size_t copied = 0;
        for (std::wsregex_iterator match(current.begin(), current.end(), expression), last; match != last; ++match) {
            const size_t position = static_cast<size_t>(match->position());
            const size_t length = static_cast<size_t>(match->length());
            if (length == 0) continue;
            result.append(current, copied, position - copied);
            const size_t end = position + length;
            // Keep words apart when filtering an expression between words.
            if (!result.empty() && end < current.size() && word_character(result.back()) && word_character(current[end])) result += L' ';
            copied = end;
        }
        result.append(current, copied, std::wstring::npos);
        current = std::move(result);
    }
    return current;
}

inline std::vector<Segment> split_languages(std::wstring_view text, Language forced = Language::Auto) {
    if (text.empty()) return {};
    if (forced != Language::Auto) return {{forced, std::wstring(text)}};
    Language current = Language::English;
    for (wchar_t c : text) {
        if (letter_language(c) != Language::Auto) { current = letter_language(c); break; }
    }
    std::vector<Segment> segments{{current, {}}};
    for (wchar_t c : text) {
        auto next = letter_language(c);
        if (next != Language::Auto && next != current) {
            segments.push_back({next, {}});
            current = next;
        }
        segments.back().text += c;
    }
    return segments;
}

inline std::wstring decode_selection(std::string_view bytes, UINT code_page) {
    if (bytes.empty()) return {};
    if (bytes.size() > 2 * 1024 * 1024) throw std::runtime_error("Select at most 2 MiB of text at a time.");
    const UINT cp = code_page ? code_page : GetACP();
    const DWORD flags = cp == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0;
    int count = MultiByteToWideChar(cp, flags, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (!count) throw std::runtime_error("The selection cannot be decoded using the editor's text encoding.");
    std::wstring result(count, L'\0');
    if (!MultiByteToWideChar(cp, flags, bytes.data(), static_cast<int>(bytes.size()), result.data(), count))
        throw std::runtime_error("Text decoding failed.");
    // Embedded NULs and XML-invalid control characters must not truncate speech.
    for (auto& c : result) if (c < 0x20 && c != L'\n' && c != L'\r' && c != L'\t') c = L' ';
    return result;
}

inline std::wstring escape_xml(std::wstring_view text) {
    std::wstring result;
    for (wchar_t c : text) {
        switch (c) {
        case L'&': result += L"&amp;"; break;
        case L'<': result += L"&lt;"; break;
        case L'>': result += L"&gt;"; break;
        case L'\"': result += L"&quot;"; break;
        case L'\'': result += L"&apos;"; break;
        default: result += c;
        }
    }
    return result;
}

inline const wchar_t* language_filter(Language language) {
    return language == Language::Korean ? L"Language=412" : L"Language=409";
}

inline std::wstring speech_xml(const std::vector<Segment>& segments) {
    std::wstring xml = L"<sapi>";
    for (const auto& segment : segments) {
        xml += L"<voice required=\"";
        xml += language_filter(segment.language);
        xml += L"\">";
        xml += escape_xml(segment.text);
        xml += L"</voice>";
    }
    return xml + L"</sapi>";
}

inline ComPtr<ISpObjectToken> find_voice(Language language) {
    ComPtr<ISpObjectTokenCategory> category;
    check(CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(category.GetAddressOf())), "Open Windows speech voices");
    check(category->SetId(SPCAT_VOICES, FALSE), "Open SAPI voice registry");
    ComPtr<IEnumSpObjectTokens> tokens;
    check(category->EnumTokens(language_filter(language), nullptr, tokens.GetAddressOf()), "Find language voice");
    ComPtr<ISpObjectToken> token;
    HRESULT hr = tokens->Next(1, token.GetAddressOf(), nullptr);
    if (FAILED(hr) || !token) {
        throw std::runtime_error(language == Language::Korean
            ? "No Korean SAPI voice is installed. Add Korean text-to-speech in Windows language settings."
            : "No English (United States) SAPI voice is installed. Add English (United States) text-to-speech in Windows language settings.");
    }
    return token;
}

inline std::wstring voice_name(Language language) {
    auto token = find_voice(language);
    wchar_t* description = nullptr;
    check(token->GetStringValue(nullptr, &description), "Read voice name");
    std::wstring result(description ? description : L"");
    CoTaskMemFree(description);
    return result;
}

class SpeechEngine {
    ComPtr<ISpVoice> voice_;
    bool paused_ = false;
    int rate_ = 0;
public:
    SpeechEngine() { check(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(voice_.GetAddressOf())), "Create Windows speech voice"); }
    ISpVoice* voice() const { return voice_.Get(); }
    bool paused() const { return paused_; }
    int rate() const { return rate_; }
    void set_rate(int value) { rate_ = std::clamp(value, -10, 10); check(voice_->SetRate(rate_), "Set speech speed"); }
    void stop() {
        // Purge while paused so no old phrase leaks out when resuming.
        check(voice_->Speak(nullptr, SPF_PURGEBEFORESPEAK | SPF_ASYNC, nullptr), "Stop speech");
        if (paused_) { check(voice_->Resume(), "Resume stopped speech queue"); paused_ = false; }
    }
    void toggle_pause() {
        if (paused_) { check(voice_->Resume(), "Resume speech"); paused_ = false; }
        else { check(voice_->Pause(), "Pause speech"); paused_ = true; }
    }
    void speak(std::wstring_view text, Language language) {
        auto segments = split_languages(text, language);
        if (segments.empty()) return;
        // Validate every required voice before replacing the current utterance.
        ComPtr<ISpObjectToken> first;
        bool english = false, korean = false;
        for (const auto& segment : segments) {
            bool& found = segment.language == Language::Korean ? korean : english;
            if (!found) {
                auto token = find_voice(segment.language);
                if (!first) first = token;
                found = true;
            }
        }
        stop();
        check(voice_->SetVoice(first.Get()), "Select language voice");
        if (segments.size() == 1) {
            check(voice_->Speak(segments.front().text.c_str(), SPF_ASYNC | SPF_IS_NOT_XML, nullptr), "Speak selection");
        } else {
            const auto xml = speech_xml(segments);
            check(voice_->Speak(xml.c_str(), SPF_ASYNC | SPF_IS_XML | SPF_PARSE_SAPI, nullptr), "Speak bilingual selection");
        }
    }
};
}
