#include "files.h"
#include "rad.h"
#include "test.h"

#include <shellapi.h>
#include <string.h>
#include <wchar.h>

#define FAKE_PERCENT 50

typedef struct program_files {
    wchar_t x86[MAX_PATH];
    wchar_t native[MAX_PATH];
} program_files;

typedef struct recorder {
    int highest_percent;
    wchar_t log[16][512];
    size_t log_count;
} recorder;

typedef struct rad_fixture {
    ev_job job;
    wchar_t folder[MAX_PATH];
    wchar_t self[MAX_PATH];
    recorder recorder;
    ev_rad_run run;
} rad_fixture;

static bool exists(const wchar_t *path)
{
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static void create_file(const wchar_t *path)
{
    CloseHandle(CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL));
}

static program_files replace_program_files(const wchar_t *folder)
{
    program_files original;
    GetEnvironmentVariableW(L"ProgramFiles(x86)", original.x86, MAX_PATH);
    GetEnvironmentVariableW(L"ProgramFiles", original.native, MAX_PATH);
    SetEnvironmentVariableW(L"ProgramFiles(x86)", folder);
    SetEnvironmentVariableW(L"ProgramFiles", folder);
    return original;
}

static void restore_program_files(const program_files *original)
{
    SetEnvironmentVariableW(L"ProgramFiles(x86)", original->x86);
    SetEnvironmentVariableW(L"ProgramFiles", original->native);
}

// Creates <folder>\RADVideo\radvideo64.exe and returns its path.
static void fake_install(const wchar_t *folder, wchar_t rad[MAX_PATH])
{
    swprintf(rad, MAX_PATH, L"%ls\\RADVideo", folder);
    CreateDirectoryW(rad, NULL);
    wcscat(rad, L"\\radvideo64.exe");
    create_file(rad);
}

static void record_progress(int percent, void *context)
{
    recorder *recorded = context;
    if (percent > recorded->highest_percent)
        recorded->highest_percent = percent;
}

static void record_log(const wchar_t *line, void *context)
{
    recorder *recorded = context;
    if (recorded->log_count < ARRAYSIZE(recorded->log))
        swprintf(recorded->log[recorded->log_count++], ARRAYSIZE(recorded->log[0]), L"%ls", line);
}

static bool logged(const recorder *recorded, const wchar_t *start)
{
    for (size_t i = 0; i < recorded->log_count; i++) {
        if (wcsncmp(recorded->log[i], start, wcslen(start)) == 0)
            return true;
    }
    return false;
}

// Fills in a fixture in place: its run points into the fixture itself, so it cannot be returned by value.
static void fixture_create(rad_fixture *fixture)
{
    *fixture = (rad_fixture){ .recorder.highest_percent = -1 };
    ev_job_create(&fixture->job);
    ev_temp_create(fixture->folder);
    GetModuleFileNameW(NULL, fixture->self, MAX_PATH);
    fixture->run = (ev_rad_run){
        .job = &fixture->job,
        .rad = fixture->self,
        .folder = fixture->folder,
        .on_progress = record_progress,
        .on_log = record_log,
        .context = &fixture->recorder,
    };
}

static void fixture_delete(rad_fixture *fixture)
{
    ev_temp_delete(fixture->folder);
    ev_job_close(&fixture->job);
}

static bool file_says(const wchar_t *folder, const wchar_t *name, const char *expected)
{
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%ls\\%ls", folder, name);
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    char text[16] = { 0 };
    DWORD read = 0;
    ReadFile(file, text, sizeof text - 1, &read, NULL);
    CloseHandle(file);
    return strcmp(text, expected) == 0;
}

static LRESULT CALLBACK fake_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_DESTROY)
        PostQuitMessage(0);
    return DefWindowProcW(window, message, wparam, lparam);
}

// Stands in for radvideo64.exe Binkc or BinkMix. Binkc needs the frames folder, BinkMix needs video.bik; without it,
// the fake shows RAD's error dialog. Its output says whether its window was visible.
int rad_test_fake(void)
{
    int argc;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool compress = wcscmp(argv[1], L"Binkc") == 0;
    const wchar_t *name = compress ? L"Bink Video Compressor" : L"Bink Audio Mixer";
    const wchar_t *output = argv[compress ? 3 : 4];
    wchar_t required[MAX_PATH];
    wcscpy(required, argv[2]);
    if (compress)
        *wcsrchr(required, L'\\') = L'\0';

    if (!exists(required)) {
        wchar_t text[MAX_PATH + 32];
        swprintf(text, ARRAYSIZE(text), L"File not found: %ls", argv[2]);
        MessageBoxW(NULL, text, name, MB_OK);
        return 8002;
    }

    HINSTANCE instance = GetModuleHandleW(NULL);
    WNDCLASSW window_class = { .lpfnWndProc = fake_window_proc, .hInstance = instance, .lpszClassName = L"RADClass" };
    RegisterClassW(&window_class);
    wchar_t title[64];
    if (compress)
        swprintf(title, ARRAYSIZE(title), L"%d%% - %ls", FAKE_PERCENT, name);
    else
        wcscpy(title, name);
    HWND window = CreateWindowW(L"RADClass", title, WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, instance, NULL);
    // Shows the window as WinMain's nCmdShow would, which Wine honors; it ignores the startup state for SW_SHOWDEFAULT.
    STARTUPINFOW startup;
    GetStartupInfoW(&startup);
    ShowWindow(window, startup.dwFlags & STARTF_USESHOWWINDOW ? startup.wShowWindow : SW_SHOWDEFAULT);

    Sleep(300);
    HANDLE file = CreateFileW(output, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    const char *visibility = IsWindowVisible(window) ? "visible" : "hidden";
    DWORD written;
    WriteFile(file, visibility, (DWORD)strlen(visibility), &written, NULL);
    CloseHandle(file);
    swprintf(title, ARRAYSIZE(title), L"%ls - Done!", name);
    SetWindowTextW(window, title);

    MSG message;
    while (GetMessageW(&message, NULL, 0, 0) > 0)
        DispatchMessageW(&message);
    LocalFree(argv);
    return 0;
}

static void locate_uses_explicit_path(void)
{
    wchar_t folder[MAX_PATH], rad[MAX_PATH], path[MAX_PATH];
    ev_temp_create(folder);
    fake_install(folder, rad);

    bool found = ev_rad_locate(rad, NULL, path);

    EXPECT(found && wcscmp(path, rad) == 0);
    ev_temp_delete(folder);
}

static void locate_rejects_missing_explicit_path(void)
{
    wchar_t folder[MAX_PATH], rad[MAX_PATH], path[MAX_PATH];
    ev_temp_create(folder);
    fake_install(folder, rad);
    program_files original = replace_program_files(folder);

    bool found = ev_rad_locate(L"C:\\evilvideo-missing\\radvideo64.exe", NULL, path);

    restore_program_files(&original);
    EXPECT(!found);
    ev_temp_delete(folder);
}

static void locate_uses_saved_path(void)
{
    wchar_t folder[MAX_PATH], rad[MAX_PATH], path[MAX_PATH];
    ev_temp_create(folder);
    fake_install(folder, rad);

    bool found = ev_rad_locate(NULL, rad, path);

    EXPECT(found && wcscmp(path, rad) == 0);
    ev_temp_delete(folder);
}

static void locate_falls_back_to_standard_install(void)
{
    wchar_t folder[MAX_PATH], rad[MAX_PATH], path[MAX_PATH];
    ev_temp_create(folder);
    fake_install(folder, rad);
    program_files original = replace_program_files(folder);

    bool found = ev_rad_locate(NULL, L"C:\\evilvideo-missing\\radvideo64.exe", path);

    restore_program_files(&original);
    EXPECT(found && wcscmp(path, rad) == 0);
    ev_temp_delete(folder);
}

static void locate_fails_when_not_installed(void)
{
    wchar_t folder[MAX_PATH], path[MAX_PATH];
    ev_temp_create(folder);
    program_files original = replace_program_files(folder);

    bool found = ev_rad_locate(NULL, NULL, path);

    restore_program_files(&original);
    EXPECT(!found);
    ev_temp_delete(folder);
}

static void binkc_args_compress_frame_sequence(void)
{
    ev_cmdline cmdline = { 0 };

    ev_rad_binkc_args(&cmdline, L"C:\\Program Files (x86)\\RADVideo\\radvideo64.exe", L"C:\\t", 150);

    EXPECT(wcscmp(cmdline.text, L"\"C:\\Program Files (x86)\\RADVideo\\radvideo64.exe\" Binkc "
                                L"C:\\t\\frames\\f??????.jpg*1-150 C:\\t\\video.bik /F29.97 /V100 /D0 /M3.0 /P8 /O") == 0);
}

static void binkc_args_quote_folder_with_spaces(void)
{
    ev_cmdline cmdline = { 0 };

    ev_rad_binkc_args(&cmdline, L"rad.exe", L"C:\\my temp", 1);

    EXPECT(wcsstr(cmdline.text, L" \"C:\\my temp\\frames\\f??????.jpg*1-1\" \"C:\\my temp\\video.bik\" ") != NULL);
}

static void binkmix_args_merge_audio(void)
{
    ev_cmdline cmdline = { 0 };

    ev_rad_binkmix_args(&cmdline, L"C:\\RAD\\radvideo64.exe", L"C:\\t");

    EXPECT(wcscmp(cmdline.text,
                  L"C:\\RAD\\radvideo64.exe BinkMix C:\\t\\video.bik C:\\t\\audio.wav C:\\t\\final.bik /L4 /O") == 0);
}

static void parse_title_reads_percentage(void)
{
    int percent;

    bool done = ev_rad_parse_title(L"57% - Bink Video Compressor", &percent);

    EXPECT(!done && percent == 57);
}

static void parse_title_reads_zero_percent(void)
{
    int percent;

    bool done = ev_rad_parse_title(L"0% - Bink Video Compressor", &percent);

    EXPECT(!done && percent == 0);
}

static void parse_title_recognizes_done(void)
{
    int percent;

    EXPECT(ev_rad_parse_title(L"Bink Video Compressor - Done!", &percent));
    EXPECT(ev_rad_parse_title(L"Bink Audio Mixer - Done!", &percent));
}

static void parse_title_ignores_title_without_state(void)
{
    int percent;

    bool done = ev_rad_parse_title(L"Bink Audio Mixer", &percent);

    EXPECT(!done && percent == -1);
}

static void parse_title_ignores_unrelated_titles(void)
{
    int percent;

    EXPECT(!ev_rad_parse_title(L"", &percent) && percent == -1);
    EXPECT(!ev_rad_parse_title(L"12 monkeys", &percent) && percent == -1);
    EXPECT(!ev_rad_parse_title(L"Done!", &percent) && percent == -1);
}

static void compress_reports_progress_and_writes_video(void)
{
    rad_fixture fixture;
    fixture_create(&fixture);
    wchar_t message[256], video[MAX_PATH];

    bool compressed = ev_rad_compress(&fixture.run, 10, message, ARRAYSIZE(message));

    swprintf(video, MAX_PATH, L"%ls\\video.bik", fixture.folder);
    EXPECT(compressed && message[0] == L'\0');
    EXPECT(fixture.recorder.highest_percent == FAKE_PERCENT);
    EXPECT(exists(video));
    fixture_delete(&fixture);
}

static void compress_hides_window_by_default(void)
{
    rad_fixture fixture;
    fixture_create(&fixture);
    wchar_t message[256];

    ev_rad_compress(&fixture.run, 10, message, ARRAYSIZE(message));

    EXPECT(file_says(fixture.folder, L"video.bik", "hidden"));
    fixture_delete(&fixture);
}

static void compress_shows_window_when_visible(void)
{
    rad_fixture fixture;
    fixture_create(&fixture);
    fixture.run.visible = true;
    wchar_t message[256];

    ev_rad_compress(&fixture.run, 10, message, ARRAYSIZE(message));

    EXPECT(file_says(fixture.folder, L"video.bik", "visible"));
    fixture_delete(&fixture);
}

static void compress_logs_command_titles_and_exit(void)
{
    rad_fixture fixture;
    fixture_create(&fixture);
    wchar_t message[256], running[MAX_PATH + 32];
    swprintf(running, ARRAYSIZE(running), L"Binkc: running %ls Binkc ", fixture.self);

    ev_rad_compress(&fixture.run, 10, message, ARRAYSIZE(message));

    EXPECT(logged(&fixture.recorder, running));
    EXPECT(logged(&fixture.recorder, L"Binkc: RADClass window \"50% - Bink Video Compressor\""));
    EXPECT(logged(&fixture.recorder, L"Binkc: RADClass window \"Bink Video Compressor - Done!\""));
    EXPECT(logged(&fixture.recorder, L"Binkc: finished; closing its window"));
    EXPECT(logged(&fixture.recorder, L"Binkc: exited with code 0 after "));
    fixture_delete(&fixture);
}

static void compress_logs_each_title_once(void)
{
    rad_fixture fixture;
    fixture_create(&fixture);
    wchar_t message[256];

    ev_rad_compress(&fixture.run, 10, message, ARRAYSIZE(message));

    int count = 0;
    for (size_t i = 0; i < fixture.recorder.log_count; i++)
        count += wcscmp(fixture.recorder.log[i], L"Binkc: RADClass window \"50% - Bink Video Compressor\"") == 0;
    EXPECT(count == 1);
    fixture_delete(&fixture);
}

static void compress_reports_error_dialog(void)
{
    rad_fixture fixture;
    fixture_create(&fixture);
    fixture.run.folder = L"C:\\evilvideo-missing";
    wchar_t message[256];

    bool compressed = ev_rad_compress(&fixture.run, 10, message, ARRAYSIZE(message));

    EXPECT(!compressed);
    EXPECT(wcscmp(message, L"File not found: C:\\evilvideo-missing\\frames\\f??????.jpg*1-10") == 0);
    EXPECT(logged(&fixture.recorder, L"Binkc: #32770 window \"Bink Video Compressor\""));
    EXPECT(logged(&fixture.recorder,
                  L"Binkc: dialog says \"File not found: C:\\evilvideo-missing\\frames\\f??????.jpg*1-10\"; closing it"));
    EXPECT(logged(&fixture.recorder, L"Binkc: exited with code 8002 after "));
    fixture_delete(&fixture);
}

static void compress_reports_missing_tool(void)
{
    rad_fixture fixture;
    fixture_create(&fixture);
    fixture.run.rad = L"C:\\evilvideo-missing\\radvideo64.exe";
    wchar_t message[256];

    bool compressed = ev_rad_compress(&fixture.run, 10, message, ARRAYSIZE(message));

    EXPECT(!compressed && wcsncmp(message, L"Could not start RAD Video Tools: ", 33) == 0);
    fixture_delete(&fixture);
}

static void compress_works_without_callbacks(void)
{
    rad_fixture fixture;
    fixture_create(&fixture);
    fixture.run.on_progress = NULL;
    fixture.run.on_log = NULL;
    wchar_t message[256];

    bool compressed = ev_rad_compress(&fixture.run, 10, message, ARRAYSIZE(message));

    EXPECT(compressed);
    fixture_delete(&fixture);
}

static void mix_writes_final_video(void)
{
    rad_fixture fixture;
    fixture_create(&fixture);
    wchar_t message[256], path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%ls\\video.bik", fixture.folder);
    create_file(path);

    bool mixed = ev_rad_mix(&fixture.run, message, ARRAYSIZE(message));

    swprintf(path, MAX_PATH, L"%ls\\final.bik", fixture.folder);
    EXPECT(mixed && message[0] == L'\0');
    EXPECT(exists(path));
    EXPECT(fixture.recorder.highest_percent == -1);
    fixture_delete(&fixture);
}

static void mix_reports_error_dialog(void)
{
    rad_fixture fixture;
    fixture_create(&fixture);
    wchar_t message[256];

    bool mixed = ev_rad_mix(&fixture.run, message, ARRAYSIZE(message));

    EXPECT(!mixed && wcsncmp(message, L"File not found: ", 16) == 0);
    fixture_delete(&fixture);
}

void rad_tests(void)
{
    RUN_TEST(locate_uses_explicit_path);
    RUN_TEST(locate_rejects_missing_explicit_path);
    RUN_TEST(locate_uses_saved_path);
    RUN_TEST(locate_falls_back_to_standard_install);
    RUN_TEST(locate_fails_when_not_installed);
    RUN_TEST(binkc_args_compress_frame_sequence);
    RUN_TEST(binkc_args_quote_folder_with_spaces);
    RUN_TEST(binkmix_args_merge_audio);
    RUN_TEST(parse_title_reads_percentage);
    RUN_TEST(parse_title_reads_zero_percent);
    RUN_TEST(parse_title_recognizes_done);
    RUN_TEST(parse_title_ignores_title_without_state);
    RUN_TEST(parse_title_ignores_unrelated_titles);
    RUN_TEST(compress_reports_progress_and_writes_video);
    RUN_TEST(compress_hides_window_by_default);
    RUN_TEST(compress_shows_window_when_visible);
    RUN_TEST(compress_logs_command_titles_and_exit);
    RUN_TEST(compress_logs_each_title_once);
    RUN_TEST(compress_reports_error_dialog);
    RUN_TEST(compress_reports_missing_tool);
    RUN_TEST(compress_works_without_callbacks);
    RUN_TEST(mix_writes_final_video);
    RUN_TEST(mix_reports_error_dialog);
}
