#include "files.h"

#include <stdio.h>
#include <wchar.h>

static const wchar_t prefix[] = L"evilvideo-";

static bool is_ascii(const wchar_t *text)
{
    for (; *text; text++) {
        if (*text > 0x7F)
            return false;
    }
    return true;
}

static bool create_in(const wchar_t *base, wchar_t folder[MAX_PATH])
{
    DWORD pid = GetCurrentProcessId();
    for (DWORD n = GetTickCount();; n++) {
        if (swprintf(folder, MAX_PATH, L"%ls\\%ls%lu-%lu", base, prefix, pid, n) < 0) {
            SetLastError(ERROR_FILENAME_EXCED_RANGE);
            return false;
        }
        if (CreateDirectoryW(folder, NULL))
            break;
        if (GetLastError() != ERROR_ALREADY_EXISTS)
            return false;
    }

    wchar_t frames[MAX_PATH];
    if (swprintf(frames, MAX_PATH, L"%ls\\frames", folder) < 0)
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
    else if (CreateDirectoryW(frames, NULL))
        return true;

    DWORD error = GetLastError();
    ev_temp_delete(folder);
    SetLastError(error);
    return false;
}

static bool temp_base(wchar_t base[MAX_PATH])
{
    DWORD length = GetTempPathW(MAX_PATH, base);
    if (length == 0 || length >= MAX_PATH)
        return false;
    base[length - 1] = L'\0';
    return true;
}

static bool program_data_base(wchar_t base[MAX_PATH])
{
    DWORD length = GetEnvironmentVariableW(L"ProgramData", base, MAX_PATH);
    return length > 0 && length < MAX_PATH && swprintf(base + length, MAX_PATH - length, L"\\evilvideo") >= 0;
}

bool ev_temp_create(wchar_t folder[MAX_PATH])
{
    wchar_t base[MAX_PATH];
    if (!temp_base(base) || !create_in(base, folder))
        return false;
    if (is_ascii(folder))
        return true;

    wchar_t short_path[MAX_PATH];
    DWORD length = GetShortPathNameW(folder, short_path, MAX_PATH);
    if (length > 0 && length < MAX_PATH && is_ascii(short_path)) {
        wcscpy(folder, short_path);
        return true;
    }

    ev_temp_delete(folder);
    if (!program_data_base(base))
        return false;
    if (!CreateDirectoryW(base, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return false;
    return create_in(base, folder);
}

bool ev_temp_delete(const wchar_t *folder)
{
    DWORD attributes = GetFileAttributesW(folder);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return false;
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY))
        return DeleteFileW(folder);

    // A link to a folder is removed as a link; following it would delete the files it points to.
    wchar_t pattern[MAX_PATH];
    if (!(attributes & FILE_ATTRIBUTE_REPARSE_POINT) && swprintf(pattern, MAX_PATH, L"%ls\\*", folder) >= 0) {
        WIN32_FIND_DATAW entry;
        HANDLE find = FindFirstFileW(pattern, &entry);
        if (find != INVALID_HANDLE_VALUE) {
            do {
                if (wcscmp(entry.cFileName, L".") == 0 || wcscmp(entry.cFileName, L"..") == 0)
                    continue;
                wchar_t child[MAX_PATH];
                if (swprintf(child, MAX_PATH, L"%ls\\%ls", folder, entry.cFileName) >= 0)
                    ev_temp_delete(child);
            } while (FindNextFileW(find, &entry));
            FindClose(find);
        }
    }
    return RemoveDirectoryW(folder);
}

static bool is_running(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return GetLastError() == ERROR_ACCESS_DENIED;

    DWORD exit_code;
    bool running = GetExitCodeProcess(process, &exit_code) && exit_code == STILL_ACTIVE;
    CloseHandle(process);
    return running;
}

static void cleanup_in(const wchar_t *base)
{
    wchar_t pattern[MAX_PATH];
    if (swprintf(pattern, MAX_PATH, L"%ls\\%ls*", base, prefix) < 0)
        return;

    WIN32_FIND_DATAW entry;
    HANDLE find = FindFirstFileW(pattern, &entry);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do {
        wchar_t *end;
        DWORD pid = wcstoul(entry.cFileName + ARRAYSIZE(prefix) - 1, &end, 10);
        wchar_t folder[MAX_PATH];
        if (*end == L'-' && !is_running(pid) && swprintf(folder, MAX_PATH, L"%ls\\%ls", base, entry.cFileName) >= 0)
            ev_temp_delete(folder);
    } while (FindNextFileW(find, &entry));
    FindClose(find);
}

void ev_temp_cleanup(void)
{
    wchar_t base[MAX_PATH];
    if (temp_base(base))
        cleanup_in(base);
    if (program_data_base(base))
        cleanup_in(base);
}

int ev_frames_count(const wchar_t *folder)
{
    wchar_t pattern[MAX_PATH];
    if (swprintf(pattern, MAX_PATH, L"%ls\\frames\\f*.jpg", folder) < 0)
        return -1;

    WIN32_FIND_DATAW entry;
    HANDLE find = FindFirstFileW(pattern, &entry);
    if (find == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : -1;

    int count = 0;
    do
        count++;
    while (FindNextFileW(find, &entry));
    FindClose(find);
    return count;
}
