# Selection TTS for Notepad++

[한국어 안내](README.ko.md) | [Download](https://github.com/janghw4/notepadpp-KorEng-tts-plugin/releases/latest)

Read selected English and Korean text with the matching Windows voice. Selection TTS is an x64 Notepad++ plugin that uses local Windows SAPI voices.

Select text such as `**Hello.**[1] 안녕하세요.` and press **Ctrl+Alt+T**. The plugin skips the asterisks and numeric reference, reads English with an English voice, and switches to Korean for the Hangul text. The document stays unchanged.

## Requirements

- Windows with x64 Notepad++. The binary does not support 32-bit or ARM64 Notepad++.
- English (US) and Korean voices available through classic Windows SAPI. The plugin looks for language IDs `409` and `412`.

The release was tested with Notepad++ 8.9.8.1, Microsoft Zira, and Microsoft Heami. Other versions and voice engines have not been verified. A voice listed in Windows Narrator may not be available to classic SAPI. **Plugins → Selection TTS → Voices and help** shows the voices the plugin can find.

## Install

1. Download `selection_tts_x64.zip` from the [latest release](https://github.com/janghw4/notepadpp-KorEng-tts-plugin/releases/latest) and extract it.
2. Save your work and close Notepad++.
3. Run `install.cmd`. If Notepad++ is under `Program Files`, right-click the file and choose **Run as administrator**.
4. Open Notepad++, select text, and press **Ctrl+Alt+T**.

For manual installation, copy `dist/SelectionTTS/SelectionTTS.dll` into `<Notepad++ folder>/plugins/SelectionTTS/SelectionTTS.dll`, then open Notepad++.

For a portable installation, run this command from the extracted package:

```powershell
.\analysis\install.ps1 -NotepadDirectory 'C:\Apps\Notepad++'
```

The installer checks the architecture and file hash. It backs up recognized earlier binaries under `%LOCALAPPDATA%\SelectionTTS\backups` before updating. It refuses to replace an unknown DLL or a DLL that Notepad++ has loaded. It does not close the editor or replace SpeechPlugin.

The ZIP includes the DLL, installer, complete corresponding source, and licenses. A GitHub source download contains source only; build it before running the installer.

## Use

Open **Plugins → Selection TTS**.

| Command | Shortcut | Action |
| --- | --- | --- |
| Read selection (Auto EN / KO) | Ctrl+Alt+T | Switch voices between English and Korean letter runs |
| Read selection in English | Menu | Use the English voice for the whole selection |
| Read selection in Korean | Menu | Use the Korean voice for the whole selection |
| Pause / Resume | Ctrl+Alt+P | Pause or continue playback |
| Stop | Ctrl+Alt+Shift+T | Stop playback |
| Slower / Faster / Normal speed | Menu | Change or reset speech speed |
| Speech speed (slider)... | Menu | Adjust speed with a slider |
| Text filters (regex)... | Menu | Set text to skip when reading |
| Voices and help | Menu | Check available voices and shortcuts |

Change conflicting shortcuts under **Settings → Shortcut Mapper → Plugin commands**.

Reading a new selection stops the previous speech. With no selection, the plugin asks you to select text. It accepts up to 2 MiB per selection and supports UTF-8 and the editor's current code page, including tested CP949 Korean.

Auto mode classifies Hangul and CJK ideographs as Korean, and Latin letters as English. Numbers and punctuation follow the surrounding letter runs. This is a character rule, so use a forced language command when needed.

## Speech speed

Open **Speech speed (slider)...**. Drag the slider or use the arrow keys. The SAPI range is `-10` to `10`, with `0` as normal speed. These values are not playback multipliers or words per minute.

Changes reach the current speech engine immediately. **OK** saves the value for future sessions. **Cancel** or closing the window restores the previous value. **Normal speed** sets the slider to `0`.

## Text filters

Open **Text filters (regex)...** and enter one ECMAScript regular expression per line. The defaults are:

```text
\*
\[\d+\]
```

These skip `*`, `[1]`, and `[123]`. Ordinary numbers and brackets such as `[name]` remain. Filters affect speech only. The plugin inserts a space when removing a match would join adjacent words.

Patterns run in order. Empty lines are ignored. Clear the list or uncheck **Enable text filters** to disable filtering. **Restore defaults** restores the two patterns above. **OK** validates and saves the settings; an invalid pattern keeps the dialog open and identifies its line. Korean text, quotes, and surrounding spaces survive saving and reloading.

## Build and test

Install Visual Studio C++ Build Tools with **Desktop development with C++** and a Windows SDK. Build with Windows PowerShell:

```powershell
.\analysis\build.ps1
.\analysis\test.ps1 -CoreOnly
.\analysis\package.ps1
```

The build produces `dist/SelectionTTS/SelectionTTS.dll`. Packaging produces `dist/releases/selection_tts_x64.zip` and its SHA-256 checksum. Choose an empty output directory with `package.ps1 -OutputDirectory <folder>` when creating another package. The build uses the static C++ runtime and does not require ATL.

GitHub Actions builds the x64 DLL and runs `-CoreOnly` tests for selection retrieval, decoding, language splitting, text filters, and settings. These tests need no installed voice or audio output. They do not verify speech synthesis or the dialogs in a real editor.

For speech tests, install both SAPI voices and use a working audio output:

```powershell
.\analysis\test.ps1
# If needed, select an output for the test only:
.\analysis\test.ps1 -AudioOutputIndex 1
```

For UI integration tests, install Python 3 and extract an official x64 Notepad++ portable package:

```powershell
python .\analysis\test_notepad.py --portable-dir 'C:\Apps\Notepad++-test'
```

Tests use synthetic text and disposable instances under the OS temporary directory. They do not read the user's open documents. Full speech tests create WAV fixtures and use a muted audio-device test for voice switching. UI tests may play short phrases.

## Troubleshooting and privacy

If a voice is missing, check **Voices and help** and ensure the voice is available to classic SAPI. To list the voices visible to SAPI:

```powershell
$voice = New-Object -ComObject SAPI.SpVoice
$voice.GetVoices() | ForEach-Object { $_.GetDescription() }
```

Speech uses the Windows default audio output. A stalled audio device can prevent playback. During local testing, one USB output also stalled an independent SAPI call; another output worked. The plugin does not select a different output or change Windows audio settings.

The plugin does not save selected text or send it to a server. It stores speed and regex settings in `SelectionTTS.ini` in the Notepad++ plugin configuration directory.

## License and credits

This project is licensed under [GPL-3.0-or-later](LICENSE). Include the corresponding source and license when distributing the DLL.

[chcg/SpeechPlugin](https://github.com/chcg/SpeechPlugin) was the starting reference for behavior and Notepad++ API headers. Selection TTS has an independent implementation in `analysis/selection_tts.cpp` and `analysis/speech_core.hpp`. Header provenance and hashes are in [reference/provenance.json](reference/provenance.json). The original Scintilla license is in [reference/scintilla_license.txt](reference/scintilla_license.txt).
