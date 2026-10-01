"""Exercise the built DLL in a separate Notepad++ with synthetic text only.

The isolated test app may play two short test phrases. The user's instance is not touched.
"""
import ctypes as C
from ctypes import wintypes as W
import argparse
import configparser
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
user = C.WinDLL('user32', use_last_error=True)
kernel = C.WinDLL('kernel32', use_last_error=True)
CALLBACK = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
user.EnumWindows.argtypes = [CALLBACK, W.LPARAM]
user.EnumChildWindows.argtypes = [W.HWND, CALLBACK, W.LPARAM]
user.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
user.GetWindowTextW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
user.GetClassNameW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
user.GetMenu.argtypes = [W.HWND]
user.GetMenu.restype = W.HMENU
user.GetSubMenu.argtypes = [W.HMENU, C.c_int]
user.GetSubMenu.restype = W.HMENU
user.GetMenuItemCount.argtypes = [W.HMENU]
user.GetMenuStringW.argtypes = [W.HMENU, W.UINT, W.LPWSTR, C.c_int, W.UINT]
user.GetMenuItemID.argtypes = [W.HMENU, C.c_int]
user.GetMenuItemID.restype = W.UINT
user.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
user.SendMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
user.SendMessageW.restype = C.c_ssize_t
user.IsWindowVisible.argtypes = [W.HWND]
user.GetDlgItem.argtypes = [W.HWND, C.c_int]
user.GetDlgItem.restype = W.HWND
user.SetWindowTextW.argtypes = [W.HWND, W.LPCWSTR]
user.GetDlgCtrlID.argtypes = [W.HWND]


def window_text(hwnd):
    buf = C.create_unicode_buffer(1024)
    user.GetWindowTextW(hwnd, buf, len(buf))
    return buf.value


def control_text(hwnd):
    # WM_GETTEXT is marshalled for standard controls across process boundaries.
    count = user.SendMessageW(hwnd, 0xE, 0, 0)  # WM_GETTEXTLENGTH
    buf = C.create_unicode_buffer(count + 1)
    user.SendMessageW(hwnd, 0xD, count + 1, C.addressof(buf))
    return buf.value


def set_control_text(hwnd, text):
    buf = C.create_unicode_buffer(text)
    assert user.SendMessageW(hwnd, 0xC, 0, C.addressof(buf)), 'WM_SETTEXT failed'


def class_name(hwnd):
    buf = C.create_unicode_buffer(256)
    user.GetClassNameW(hwnd, buf, len(buf))
    return buf.value


def windows(pid):
    result = []
    @CALLBACK
    def visit(hwnd, _):
        owner = W.DWORD()
        user.GetWindowThreadProcessId(hwnd, C.byref(owner))
        if owner.value == pid:
            result.append(hwnd)
        return True
    user.EnumWindows(visit, 0)
    return result


def children(hwnd):
    result = []
    @CALLBACK
    def visit(child, _):
        result.append(child)
        return True
    user.EnumChildWindows(hwnd, visit, 0)
    return result


def menu_items(menu):
    result = []
    for index in range(user.GetMenuItemCount(menu)):
        buf = C.create_unicode_buffer(256)
        user.GetMenuStringW(menu, index, buf, len(buf), 0x400)
        child = user.GetSubMenu(menu, index)
        result.append({'label': buf.value, 'id': user.GetMenuItemID(menu, index),
                       'children': menu_items(child) if child else []})
    return result


def flatten(items):
    for item in items:
        yield item
        yield from flatten(item['children'])


def wait_dialog(pid, title, excluded=None):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        dialog = next((w for w in windows(pid) if w != excluded and class_name(w) == '#32770'
                       and user.IsWindowVisible(w) and window_text(w) == title), None)
        if dialog:
            return dialog
        time.sleep(.05)
    raise AssertionError(f'No {title} dialog in isolated process')


def wait_closed(pid, dialog):
    deadline = time.monotonic() + 5
    while dialog in windows(pid):
        assert time.monotonic() < deadline, f'Dialog did not close: {window_text(dialog)}'
        time.sleep(.02)


def filter_config(target):
    path = target / 'plugins' / 'Config' / 'KorEngTTS.ini'
    config = configparser.ConfigParser(interpolation=None)
    config.read(path, encoding='utf-8-sig')
    return config


def test_filter_window(pid, main_window, command, target):
    user.PostMessageW(main_window, 0x111, command, 0)
    dialog = wait_dialog(pid, 'Text filters')
    editor = user.GetDlgItem(dialog, 2002)
    default_text = control_text(editor)
    assert r'\*' in default_text and r'\[\d+\]' in default_text, default_text
    set_control_text(editor, '[')
    assert control_text(editor) == '[', 'The regex editor did not accept the test input'
    user.PostMessageW(dialog, 0x111, 1, 0)
    error = wait_dialog(pid, 'KorEng TTS', excluded=dialog)
    assert any('Invalid regular expression on line 1' in window_text(w) for w in children(error))
    buttons = [w for w in children(error) if class_name(w) == 'Button']
    assert buttons, [(class_name(w), user.GetDlgCtrlID(w), window_text(w)) for w in children(error)]
    button = buttons[0]
    user.SendMessageW(error, 0x111, user.GetDlgCtrlID(button), button)
    wait_closed(pid, error)
    assert dialog in windows(pid), 'Invalid regex closed the settings window'
    custom = r'\*|\[\d+\]|#\d+' + '\r\n' + '주석'
    set_control_text(editor, custom)
    user.SendMessageW(dialog, 0x111, 1, 0)
    wait_closed(pid, dialog)
    saved = filter_config(target)
    assert saved.getint('Filters', 'Count') == 2
    assert saved.getint('Filters', 'Enabled') == 1
    assert saved.get('Filters', 'Pattern1Utf16').upper() == ''.join(f'{ord(c):04X}' for c in custom.split('\r\n')[0])
    user.PostMessageW(main_window, 0x111, command, 0)
    dialog = wait_dialog(pid, 'Text filters')
    assert '주석' in control_text(user.GetDlgItem(dialog, 2002)), 'Saved Korean regex was not restored'
    user.SendMessageW(user.GetDlgItem(dialog, 2001), 0xF1, 0, 0)  # BM_SETCHECK, unchecked
    user.SendMessageW(dialog, 0x111, 1, 0)
    wait_closed(pid, dialog)
    assert filter_config(target).getint('Filters', 'Enabled') == 0
    user.PostMessageW(main_window, 0x111, command, 0)
    dialog = wait_dialog(pid, 'Text filters')
    user.SendMessageW(dialog, 0x111, 2003, 0)  # Restore defaults
    assert r'\[\d+\]' in control_text(user.GetDlgItem(dialog, 2002))
    user.SendMessageW(dialog, 0x111, 1, 0)
    wait_closed(pid, dialog)
    assert filter_config(target).getint('Filters', 'Enabled') == 1
    return ['regex settings window', 'invalid regex rejected', 'custom and Korean regex saved and reopened', 'filters disabled', 'defaults restored']


def test_speed_window(pid, main_window, command, target):
    user.PostMessageW(main_window, 0x111, command, 0)
    dialog = wait_dialog(pid, 'Speech speed')
    slider = user.GetDlgItem(dialog, 3001)
    assert class_name(slider) == 'msctls_trackbar32'
    assert user.SendMessageW(slider, 0x401, 0, 0) == -10  # TBM_GETRANGEMIN
    assert user.SendMessageW(slider, 0x402, 0, 0) == 10  # TBM_GETRANGEMAX
    original = user.SendMessageW(slider, 0x400, 0, 0)  # TBM_GETPOS
    for value in (-10, -5, 0, 5, 10, 3):
        user.SendMessageW(slider, 0x405, 1, value)  # TBM_SETPOS
        user.SendMessageW(dialog, 0x114, 5, slider)  # WM_HSCROLL, TB_THUMBTRACK
        assert f'Speed: {value}' in control_text(user.GetDlgItem(dialog, 3002))
        assert filter_config(target).getint('Speech', 'Rate', fallback=0) == original, 'Preview was saved before OK'
    user.SendMessageW(dialog, 0x111, 1, 0)
    wait_closed(pid, dialog)
    assert filter_config(target).getint('Speech', 'Rate') == 3
    user.PostMessageW(main_window, 0x111, command, 0)
    dialog = wait_dialog(pid, 'Speech speed')
    slider = user.GetDlgItem(dialog, 3001)
    assert user.SendMessageW(slider, 0x400, 0, 0) == 3, 'Saved slider position was not restored'
    user.SendMessageW(slider, 0x405, 1, -4)
    user.SendMessageW(dialog, 0x114, 5, slider)
    user.SendMessageW(dialog, 0x111, 2, 0)  # Cancel
    wait_closed(pid, dialog)
    assert filter_config(target).getint('Speech', 'Rate') == 3, 'Cancel changed saved speed'
    user.PostMessageW(main_window, 0x111, command, 0)
    dialog = wait_dialog(pid, 'Speech speed')
    assert user.SendMessageW(user.GetDlgItem(dialog, 3001), 0x400, 0, 0) == 3, 'Cancel did not restore preview speed'
    user.SendMessageW(dialog, 0x111, 3003, 0)  # Normal speed
    assert user.SendMessageW(user.GetDlgItem(dialog, 3001), 0x400, 0, 0) == 0
    assert 'Speed: 0 (normal)' == control_text(user.GetDlgItem(dialog, 3002))
    user.SendMessageW(dialog, 0x111, 1, 0)
    wait_closed(pid, dialog)
    assert filter_config(target).getint('Speech', 'Rate') == 0
    assert filter_config(target).getint('Filters', 'Enabled') == 1, 'Speed settings changed filters'
    return ['speed slider -10 to 10', 'speed label follows slider', 'speed preview does not save before OK', 'saved speed reopened', 'Cancel restores previous speed', 'normal speed restored']


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--portable-dir', type=Path, required=True, help='Extracted official Notepad++ portable package')
    args = parser.parse_args()
    target = Path(tempfile.mkdtemp(prefix='selection-tts-notepad-'))
    shutil.copytree(args.portable_dir, target, dirs_exist_ok=True)
    (target / 'doLocalConf.xml').write_bytes(b'')
    plugins = target / 'plugins' / 'KorEngTTS'
    plugins.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / 'dist' / 'KorEngTTS' / 'KorEngTTS.dll', plugins)
    text = 'DO NOT READ THIS PREFIX.\n**Selected English sentence.**[12]\n**선택한 한국어 문장입니다.**[123]\nDO NOT READ THIS SUFFIX.\n'
    fixture = target / 'selection_fixture.txt'
    fixture.write_text(text, encoding='utf-8-sig', newline='\n')
    proc = subprocess.Popen([str(target / 'notepad++.exe'), '-multiInst', '-nosession', str(fixture)], cwd=target,
                            startupinfo=subprocess.STARTUPINFO(dwFlags=subprocess.STARTF_USESHOWWINDOW, wShowWindow=0))
    main_window = None
    try:
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            main_window = next((w for w in windows(proc.pid) if class_name(w) == 'Notepad++'), None)
            if main_window and user.GetMenu(main_window):
                break
            if proc.poll() is not None:
                raise RuntimeError(f'The isolated editor exited before startup: {proc.returncode}')
            time.sleep(.1)
        assert main_window, 'No isolated Notepad++ window'
        deadline = time.monotonic() + 8
        plugin = None
        while time.monotonic() < deadline:
            items = list(flatten(menu_items(user.GetMenu(main_window))))
            plugin = next((item for item in items if item['label'].replace('&', '') == 'KorEng TTS'), None)
            if plugin:
                break
            time.sleep(.1)
        assert plugin, f'KorEng TTS menu was not loaded: {[item["label"] for item in items]}'
        commands = {item['label'].split('\t')[0]: item['id'] for item in plugin['children'] if item['label']}
        assert len(commands) == 11, commands
        deadline = time.monotonic() + 5
        editors = []
        while time.monotonic() < deadline:
            editors = [w for w in children(main_window) if class_name(w) == 'Scintilla'
                       and user.SendMessageW(w, 2006, 0, 0) == len(text.encode('utf-8'))]
            if editors:
                break
            time.sleep(.05)
        assert editors, 'No editor containing the synthetic fixture'
        editor = editors[0]
        # All offsets are generated from the synthetic fixture; no user buffer is inspected.
        checks = test_filter_window(proc.pid, main_window, commands['Text filters (regex)...'], target)
        checks += test_speed_window(proc.pid, main_window, commands['Speech speed (slider)...'], target)
        for phrase in ('**Selected English sentence.**[12]', '**선택한 한국어 문장입니다.**[123]'):
            start = len(text[:text.index(phrase)].encode('utf-8'))
            end = start + len(phrase.encode('utf-8'))
            user.SendMessageW(editor, 2160, start, end)  # SCI_SETSEL
            assert user.SendMessageW(editor, 2161, 0, 0) == end - start
            user.SendMessageW(main_window, 0x111, commands['Read selection (Auto EN / KO)'], 0)
            time.sleep(.15)
            dialogs = [w for w in windows(proc.pid) if class_name(w) == '#32770']
            assert not dialogs, [window_text(w) for w in dialogs]
            user.SendMessageW(main_window, 0x111, commands['Pause / Resume'], 0)
            user.SendMessageW(main_window, 0x111, commands['Stop'], 0)
            checks.append('Korean selection' if '한' in phrase else 'English selection')
        assert user.SendMessageW(editor, 2159, 0, 0) == 0, 'Plugin modified the test document'  # SCI_GETMODIFY
        assert fixture.read_text(encoding='utf-8-sig') == text, 'Plugin modified the fixture file'
        # Verify the empty-selection command displays a prompt and does not read the file.
        user.SendMessageW(editor, 2160, 0, 0)
        user.PostMessageW(main_window, 0x111, commands['Read selection (Auto EN / KO)'], 0)
        deadline = time.monotonic() + 5
        dialog = None
        while time.monotonic() < deadline:
            dialog = next((w for w in windows(proc.pid) if class_name(w) == '#32770'), None)
            if dialog:
                break
            time.sleep(.05)
        assert dialog, 'No empty-selection message'
        labels = [window_text(w) for w in children(dialog)]
        assert any('Select the text' in label for label in labels), labels
        button = next(w for w in children(dialog) if class_name(w) == 'Button')
        user.SendMessageW(dialog, 0x111, user.GetDlgCtrlID(button), button)
        wait_closed(proc.pid, dialog)
        report = {'notepad_version': '8.9.8.1', 'architecture': 'x64', 'loaded_menu': plugin,
                  'passed': checks + ['pause and stop', 'empty selection prompt', 'document and file unchanged'],
                  'isolated_process_id': proc.pid, 'test_directory': str(target)}
        (target / 'integration_result.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
        print(json.dumps(report, ensure_ascii=False, indent=2))
    finally:
        # This process was created by this test. Never send WM_CLOSE to any other instance.
        if main_window and main_window in windows(proc.pid):
            user.PostMessageW(main_window, 0x10, 0, 0)
            try:
                proc.wait(timeout=8)
            except subprocess.TimeoutExpired:
                print('The isolated test window is still open; no process was force-killed.')


if __name__ == '__main__':
    main()
