#include "files.h"
#include "settings.h"
#include "test.h"

#include <string.h>
#include <wchar.h>

typedef struct appdata_fixture {
    wchar_t folder[MAX_PATH];
    wchar_t original[MAX_PATH];
} appdata_fixture;

// Points APPDATA at a new temporary folder, so the tests never touch the user's settings.
static appdata_fixture replace_appdata(void)
{
    appdata_fixture fixture;
    ev_temp_create(fixture.folder);
    GetEnvironmentVariableW(L"APPDATA", fixture.original, MAX_PATH);
    SetEnvironmentVariableW(L"APPDATA", fixture.folder);
    return fixture;
}

static void restore_appdata(const appdata_fixture *fixture)
{
    SetEnvironmentVariableW(L"APPDATA", fixture->original);
    ev_temp_delete(fixture->folder);
}

static void write_file(const wchar_t *path, const char *text)
{
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written;
    WriteFile(file, text, (DWORD)strlen(text), &written, NULL);
    CloseHandle(file);
}

static void load_uses_defaults_without_file(void)
{
    appdata_fixture fixture = replace_appdata();
    ev_settings settings;

    ev_settings_load(&settings);

    restore_appdata(&fixture);
    EXPECT(settings.rad[0] == L'\0');
    EXPECT(settings.game == NULL);
    EXPECT(settings.letterbox);
}

static void save_then_load_round_trips(void)
{
    appdata_fixture fixture = replace_appdata();
    ev_settings saved = { .rad = L"C:\\\x30C4\x30FC\x30EB\\radvideo64.exe", .game = ev_game_find(L"tssm") };
    ev_settings loaded;

    bool written = ev_settings_save(&saved);
    ev_settings_load(&loaded);

    restore_appdata(&fixture);
    EXPECT(written);
    EXPECT(wcscmp(loaded.rad, saved.rad) == 0);
    EXPECT(loaded.game == saved.game);
    EXPECT(!loaded.letterbox);
}

static void save_replaces_earlier_settings(void)
{
    appdata_fixture fixture = replace_appdata();
    ev_settings first = { .rad = L"C:\\RAD\\radvideo64.exe", .game = ev_game_find(L"bfbb"), .letterbox = false };
    ev_settings second = { .game = ev_game_find(L"rotu"), .letterbox = true };
    ev_settings loaded;
    ev_settings_save(&first);

    ev_settings_save(&second);
    ev_settings_load(&loaded);

    restore_appdata(&fixture);
    EXPECT(loaded.rad[0] == L'\0');
    EXPECT(loaded.game == second.game);
    EXPECT(loaded.letterbox);
}

static void load_ignores_unknown_game(void)
{
    appdata_fixture fixture = replace_appdata();
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%ls\\evilvideo", fixture.folder);
    CreateDirectoryW(path, NULL);
    wcscat(path, L"\\evilvideo.ini");
    write_file(path, "[evilvideo]\r\ngame=rat\r\n");
    ev_settings settings;

    ev_settings_load(&settings);

    restore_appdata(&fixture);
    EXPECT(settings.game == NULL);
    EXPECT(settings.letterbox);
}

void settings_tests(void)
{
    RUN_TEST(load_uses_defaults_without_file);
    RUN_TEST(save_then_load_round_trips);
    RUN_TEST(save_replaces_earlier_settings);
    RUN_TEST(load_ignores_unknown_game);
}
