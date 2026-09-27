#include "files.h"
#include "proc.h"
#include "test.h"

#include <wchar.h>

void add_test_child(ev_cmdline *cmdline, const wchar_t *mode);

static bool exists(const wchar_t *path)
{
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static void create_file(const wchar_t *folder, const wchar_t *name)
{
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%ls\\%ls", folder, name);
    CloseHandle(CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL));
}

static DWORD exited_pid(void)
{
    HANDLE job = ev_job_create();
    ev_cmdline cmdline = { 0 };
    add_test_child(&cmdline, L"output");
    HANDLE process;
    ev_process_start(job, &cmdline, &process);
    WaitForSingleObject(process, INFINITE);
    DWORD pid = GetProcessId(process);
    CloseHandle(process);
    CloseHandle(job);
    return pid;
}

static void temp_create_makes_ascii_folder_with_frames(void)
{
    wchar_t folder[MAX_PATH];

    bool created = ev_temp_create(folder);

    wchar_t frames[MAX_PATH];
    swprintf(frames, MAX_PATH, L"%ls\\frames", folder);
    EXPECT(created && exists(frames));
    for (const wchar_t *c = folder; created && *c; c++)
        EXPECT(*c <= 0x7F);
    ev_temp_delete(folder);
}

static void temp_create_avoids_non_ascii_temp_path(void)
{
    wchar_t original[MAX_PATH], base[MAX_PATH];
    GetEnvironmentVariableW(L"TMP", original, MAX_PATH);
    GetTempPathW(MAX_PATH, base);
    wcscat(base, L"evilvideo test 日本");
    CreateDirectoryW(base, NULL);
    SetEnvironmentVariableW(L"TMP", base);
    wchar_t folder[MAX_PATH];

    bool created = ev_temp_create(folder);

    SetEnvironmentVariableW(L"TMP", original);
    EXPECT(created && exists(folder));
    for (const wchar_t *c = folder; created && *c; c++)
        EXPECT(*c <= 0x7F);
    ev_temp_delete(folder);
    RemoveDirectoryW(base);
}

static void temp_create_makes_a_new_folder_each_time(void)
{
    wchar_t first[MAX_PATH], second[MAX_PATH];

    bool created = ev_temp_create(first) && ev_temp_create(second);

    EXPECT(created && wcscmp(first, second) != 0);
    ev_temp_delete(first);
    ev_temp_delete(second);
}

static void temp_delete_removes_folder_and_contents(void)
{
    wchar_t folder[MAX_PATH];
    ev_temp_create(folder);
    create_file(folder, L"frames\\f000001.jpg");
    create_file(folder, L"audio.wav");

    bool deleted = ev_temp_delete(folder);

    EXPECT(deleted && !exists(folder));
}

static void temp_cleanup_removes_folders_of_exited_processes(void)
{
    wchar_t base[MAX_PATH];
    GetTempPathW(MAX_PATH, base);
    wchar_t abandoned[MAX_PATH];
    swprintf(abandoned, MAX_PATH, L"%lsevilvideo-%lu-1", base, exited_pid());
    CreateDirectoryW(abandoned, NULL);
    create_file(abandoned, L"audio.wav");

    ev_temp_cleanup();

    EXPECT(!exists(abandoned));
}

static void temp_cleanup_keeps_folders_of_running_processes(void)
{
    wchar_t folder[MAX_PATH];
    ev_temp_create(folder);

    ev_temp_cleanup();

    EXPECT(exists(folder));
    ev_temp_delete(folder);
}

static void frames_count_counts_jpegs_in_frames(void)
{
    wchar_t folder[MAX_PATH];
    ev_temp_create(folder);
    create_file(folder, L"frames\\f000001.jpg");
    create_file(folder, L"frames\\f000002.jpg");
    create_file(folder, L"audio.wav");

    int count = ev_frames_count(folder);

    EXPECT(count == 2);
    ev_temp_delete(folder);
}

static void frames_count_is_zero_without_frames(void)
{
    wchar_t folder[MAX_PATH];
    ev_temp_create(folder);

    int count = ev_frames_count(folder);

    EXPECT(count == 0);
    ev_temp_delete(folder);
}

void files_tests(void)
{
    RUN_TEST(temp_create_makes_ascii_folder_with_frames);
    RUN_TEST(temp_create_avoids_non_ascii_temp_path);
    RUN_TEST(temp_create_makes_a_new_folder_each_time);
    RUN_TEST(temp_delete_removes_folder_and_contents);
    RUN_TEST(temp_cleanup_removes_folders_of_exited_processes);
    RUN_TEST(temp_cleanup_keeps_folders_of_running_processes);
    RUN_TEST(frames_count_counts_jpegs_in_frames);
    RUN_TEST(frames_count_is_zero_without_frames);
}
