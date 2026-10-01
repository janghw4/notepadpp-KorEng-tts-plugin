# Changelog

## 1.3.0 (2026-09-30)

First GitHub release.

- Read selected English and Korean text with the matching local SAPI voice. Auto mode switches between letter runs; separate commands force either language.
- Pause, resume, or stop playback with menu commands and shortcuts.
- Adjust speech speed with a slider from -10 to 10. OK saves the value; Cancel restores it.
- Edit regex filters in a native settings dialog. Defaults skip asterisks and numeric references such as `[12]`.
- Preserve the editor text while filtering speech. Support UTF-8 and the editor code page, including tested CP949 Korean.
- Include an x64 build script, installer, source, and GPL license in the release package.

Local validation: 50 core checks, 84 full native checks including English/Korean synthesis and voice switching, and 16 checks in an isolated Notepad++ 8.9.8.1 instance. Speech tests used a working audio output selected explicitly for testing. GitHub Actions checks the build and core behavior without voices or audio hardware.
