# Selection TTS for Notepad++

선택한 영어는 영어 음성으로, 한국어는 한국어 음성으로 읽는 x64 Notepad++ 플러그인.
Windows에 설치된 SAPI 음성을 사용한다. 이 컴퓨터에서는 영어 Zira와 한국어 Heami를 확인했다.

[English guide](README.md) | [최신 버전 다운로드](https://github.com/janghw4/notepadpp-KorEng-tts-plugin/releases/latest)

Notepad++ 8.9.8.1 x64에서 검증했다. 32비트·ARM64용 바이너리는 제공하지 않는다.
Windows Narrator에 표시되는 음성이라도 classic SAPI에서 사용할 수 없을 수 있다.
**Voices and help** 메뉴에서 플러그인이 찾은 음성을 확인할 수 있다.

## 설치

1. [최신 릴리스](https://github.com/janghw4/notepadpp-KorEng-tts-plugin/releases/latest)의 `selection_tts_x64.zip`을 내려받아 압축을 푼다.
2. 작업을 저장하고 Notepad++를 닫는다.
3. `install.cmd`를 실행한다. `Program Files`에 설치했다면 마우스 오른쪽 버튼으로 눌러 **관리자 권한으로 실행**한다.
4. Notepad++를 실행한 뒤 텍스트를 선택하고 **Ctrl+Alt+T**를 누른다.

수동 설치: `dist/SelectionTTS/SelectionTTS.dll`을 Notepad++ 설치 폴더의
`plugins/SelectionTTS/SelectionTTS.dll`에 복사하고 Notepad++를 다시 실행한다.
포터블 버전은 `analysis/install.ps1 -NotepadDirectory '포터블 폴더'`로 설치할 수 있다.
GitHub의 소스 ZIP에는 DLL이 없으므로 먼저 빌드해야 한다. 릴리스 ZIP에는 DLL과 전체 소스가 들어 있다.
설치 프로그램은 기존 SpeechPlugin이나 편집 중인 문서를 변경하지 않으며 앱을 종료하지 않는다.
같은 파일은 재설치하지 않는다. 확인된 이전 버전은 로컬 백업을 보존한 뒤 업데이트하며,
알 수 없는 DLL이 있으면 중단한다. 실행 중인 Notepad++의 DLL은 덮어쓰지 않는다.

## 사용

메뉴: **Plugins → Selection TTS**

| 명령 | 단축키 | 동작 |
|---|---|---|
| Read selection (Auto EN / KO) | Ctrl+Alt+T | 영어·한국어 글자 구간에 맞춰 음성을 전환 |
| Read selection in English | 메뉴에서 선택 | 영어 음성으로 읽기 |
| Read selection in Korean | 메뉴에서 선택 | 한국어 음성으로 읽기 |
| Pause / Resume | Ctrl+Alt+P | 일시정지·다시 재생 |
| Stop | Ctrl+Alt+Shift+T | 재생 중지 |
| Slower / Faster / Normal speed | 메뉴에서 선택 | 속도 조절·기본 속도로 복원 |
| Speech speed (slider)... | 메뉴에서 선택 | 슬라이더로 속도 조절·저장 |
| Text filters (regex)... | 메뉴에서 선택 | 읽을 때 제외할 정규식을 입력·저장 |
| Voices and help | 메뉴에서 선택 | 사용 가능한 음성과 도움말 확인 |

선택 영역이 없으면 텍스트를 선택하라는 안내를 표시한다. 새 선택 영역을 읽으면 이전 재생은 중지된다.
기본 필터는 `*`와 `[1]`, `[12]`처럼 숫자만 들어 있는 대괄호를 건너뛴다.
일반 숫자나 `[이름]`은 그대로 읽으며, 문서의 원문은 바꾸지 않는다.
단축키가 다른 플러그인과 겹치면 **Settings → Shortcut Mapper → Plugin commands**에서 변경한다.

자동 모드는 한글·한자와 라틴 글자를 구분한다. 숫자와 문장부호는 앞뒤 글자 구간에 붙는다.
이는 언어 모델을 이용한 판별이 아니므로, 한 음성으로 읽고 싶으면 해당 언어 메뉴를 사용한다.
UTF-8, Notepad++가 Unicode로 변환한 문서, 편집기의 코드 페이지를 지원한다. CP949 한글도 검증했다.
한 번에 선택할 수 있는 텍스트는 2 MiB까지다.

Windows SAPI의 영어(미국)·한국어 음성이 필요하다. 음성이 없으면 설치 안내를 표시한다.
선택한 텍스트를 파일로 저장하거나 서버로 보내지 않는다. 설정 파일에는 읽기 속도와 정규식 필터를 저장한다.
음성 품질은 설치된 Windows 음성에 따라 달라진다.

## 슬라이더로 속도 조절

**Plugins → Selection TTS → Speech speed (slider)...**를 연다.
슬라이더를 왼쪽으로 움직이면 느려지고 오른쪽으로 움직이면 빨라진다.
범위는 Windows 음성의 `-10`부터 `10`까지이며, `0`이 기본 속도다.
배속이나 분당 단어 수를 나타내는 값은 아니다.

현재 값은 창 위에 표시된다. 조절한 값은 현재 음성 엔진에 반영한다.
`OK`를 누르면 다음 실행에도 사용할 값을 저장한다.
`Cancel` 또는 창 닫기는 열기 전 속도로 되돌린다. `Normal speed`는 슬라이더를 `0`으로 돌린다.
마우스와 방향키로 조절할 수 있다.

## 정규식 필터 설정

**Plugins → Selection TTS → Text filters (regex)...**를 열고 한 줄에 정규식 하나씩 입력한다.
입력한 정규식과 일치하는 내용은 읽기 전에 제외한다. 문서에는 원문이 그대로 남는다.

기본값:

```text
\*
\[\d+\]
```

첫 줄은 별표를, 둘째 줄은 `[숫자]`를 건너뛴다. 정규식은 ECMAScript 문법을 사용한다.
여러 줄은 위에서 아래 순서대로 적용한다. 빈 줄은 무시하고, 패턴이 없으면 필터를 적용하지 않는다.
`Enable text filters`를 해제하면 필터를 끈다. `Restore defaults`는 위 두 패턴을 복원한다.
`OK`를 누르면 설정을 저장한다. 잘못된 정규식이 있으면 해당 줄의 오류를 표시하고 창을 유지한다.
필터를 제외하며 단어가 붙지 않게 필요한 공백을 보존한다.
한글·따옴표·공백을 포함한 정규식도 다음 실행에서 복원된다.

## 빌드와 검증

Windows, Visual Studio C++ Build Tools, Windows SDK가 필요하다. ATL과 별도 런타임 설치는 필요 없다.

```powershell
.\analysis\build.ps1
.\analysis\test.ps1 -CoreOnly
.\analysis\package.ps1
# 영어·한국어 SAPI 음성과 오디오 출력이 준비된 환경에서:
.\analysis\test.ps1
python .\analysis\test_notepad.py --portable-dir '공식 x64 포터블 패키지를 푼 폴더'
```

테스트는 OS 임시 폴더에 합성 문장과 WAV 파일을 만들고, 별도 Notepad++ 인스턴스만 사용한다.
사용자의 열린 문서를 읽거나 수정하지 않는다. 통합 테스트는 짧은 테스트 음성을 재생할 수 있다.
자동 빌드는 `-CoreOnly`로 선택 영역·문자 인코딩·언어 구간·정규식·설정 저장을 검사한다.
이 검사는 음성과 오디오 장치가 필요 없으며, 실제 음성 합성과 편집기 창 검증은 별도로 실행한다.
`package.ps1`은 `dist/releases/selection_tts_x64.zip`과 SHA-256 파일을 만든다.
기존 ZIP이 있으면 중단하므로 재생성할 때는 `-OutputDirectory '빈 폴더'`를 지정한다.
기본 오디오 장치를 사용할 수 없으면 `test.ps1 -AudioOutputIndex <번호>`로
테스트 음성에만 다른 출력 장치를 지정할 수 있다. Windows의 기본 출력 설정은 바꾸지 않는다.

플러그인은 Windows의 기본 오디오 출력을 사용한다. 로컬 검증에서 한 USB 출력은 독립 SAPI 호출도
멈췄고 다른 출력은 정상 동작했다. 재생이 시작되지 않으면 Windows 출력 장치도 확인해야 한다.

## 출처와 라이선스

[chcg/SpeechPlugin](https://github.com/chcg/SpeechPlugin)의 동작을 검토하고 Notepad++ API 헤더를 참조했다.
플러그인 구현은 이 폴더의 `analysis/selection_tts.cpp`와 `analysis/speech_core.hpp`에 있다.
헤더 출처와 해시는 `reference/provenance.json`에 있다. 전체 프로젝트는 GPL-3.0-or-later로 제공한다.
Scintilla 헤더의 원래 라이선스도 `reference/scintilla_license.txt`에 포함했다.
배포할 때 소스와 `LICENSE`를 함께 제공한다. 릴리스 ZIP에는 전체 소스와 라이선스를 포함한다.
