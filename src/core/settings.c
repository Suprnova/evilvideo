#include "settings.h"

#include <stdio.h>

static const wchar_t section[] = L"evilvideo";

static bool settings_path(wchar_t path[MAX_PATH], bool create_folder)
{
    wchar_t appdata[MAX_PATH];
    DWORD length = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    if (length == 0 || length >= MAX_PATH || swprintf(path, MAX_PATH, L"%ls\\evilvideo", appdata) < 0)
        return false;
    if (create_folder && !CreateDirectoryW(path, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return false;
    return swprintf(path, MAX_PATH, L"%ls\\evilvideo\\evilvideo.ini", appdata) >= 0;
}

// WritePrivateProfileStringW writes a new file in the ANSI code page, which loses any character of the RAD path outside
// it. A file that starts with a UTF-16 byte order mark is kept in UTF-16.
static bool create_unicode_file(const wchar_t *path)
{
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_EXISTS;

    static const BYTE bom[] = { 0xFF, 0xFE };
    DWORD written;
    bool written_all = WriteFile(file, bom, sizeof bom, &written, NULL) && written == sizeof bom;
    CloseHandle(file);
    return written_all;
}

void ev_settings_load(ev_settings *settings)
{
    *settings = (ev_settings){ .letterbox = true };
    wchar_t path[MAX_PATH];
    if (!settings_path(path, false))
        return;

    wchar_t game[32];
    GetPrivateProfileStringW(section, L"rad", L"", settings->rad, MAX_PATH, path);
    GetPrivateProfileStringW(section, L"game", L"", game, ARRAYSIZE(game), path);
    settings->game = ev_game_find(game);
    settings->letterbox = GetPrivateProfileIntW(section, L"letterbox", 1, path) != 0;
}

bool ev_settings_save(const ev_settings *settings)
{
    wchar_t path[MAX_PATH];
    return settings_path(path, true) && create_unicode_file(path) &&
           WritePrivateProfileStringW(section, L"rad", settings->rad, path) &&
           WritePrivateProfileStringW(section, L"game", settings->game ? settings->game->id : L"", path) &&
           WritePrivateProfileStringW(section, L"letterbox", settings->letterbox ? L"1" : L"0", path);
}
