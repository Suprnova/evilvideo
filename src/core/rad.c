#include "rad.h"

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

typedef struct watch_state {
    DWORD pid;
    bool done;
    int percent;
    wchar_t *message;
    size_t message_size;
} watch_state;

static bool is_file(const wchar_t *path)
{
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static bool standard_install(const wchar_t *variable, wchar_t path[MAX_PATH])
{
    wchar_t base[MAX_PATH];
    DWORD length = GetEnvironmentVariableW(variable, base, MAX_PATH);
    return length > 0 && length < MAX_PATH && swprintf(path, MAX_PATH, L"%ls\\RADVideo\\radvideo64.exe", base) >= 0 &&
           is_file(path);
}

bool ev_rad_locate(const wchar_t *explicit_path, const wchar_t *saved_path, wchar_t path[MAX_PATH])
{
    const wchar_t *given = explicit_path ? explicit_path : saved_path;
    if (given) {
        DWORD length = GetFullPathNameW(given, MAX_PATH, path, NULL);
        if (length > 0 && length < MAX_PATH && is_file(path))
            return true;
        if (explicit_path) {
            SetLastError(ERROR_FILE_NOT_FOUND);
            return false;
        }
    }

    if (standard_install(L"ProgramFiles(x86)", path) || standard_install(L"ProgramFiles", path))
        return true;
    SetLastError(ERROR_FILE_NOT_FOUND);
    return false;
}

void ev_rad_binkc_args(ev_cmdline *cmdline, const wchar_t *rad, const wchar_t *folder, int frames)
{
    wchar_t input[MAX_PATH + 32], output[MAX_PATH + 16];
    swprintf(input, ARRAYSIZE(input), L"%ls\\frames\\f??????.jpg*1-%d", folder, frames);
    swprintf(output, ARRAYSIZE(output), L"%ls\\video.bik", folder);
    const wchar_t *args[] = { rad, L"Binkc", input, output, L"/F29.97", L"/V100", L"/D0", L"/M3.0", L"/P8", L"/O" };
    for (size_t i = 0; i < ARRAYSIZE(args); i++)
        ev_cmdline_add(cmdline, args[i]);
}

void ev_rad_binkmix_args(ev_cmdline *cmdline, const wchar_t *rad, const wchar_t *folder)
{
    wchar_t video[MAX_PATH + 16], audio[MAX_PATH + 16], output[MAX_PATH + 16];
    swprintf(video, ARRAYSIZE(video), L"%ls\\video.bik", folder);
    swprintf(audio, ARRAYSIZE(audio), L"%ls\\audio.wav", folder);
    swprintf(output, ARRAYSIZE(output), L"%ls\\final.bik", folder);
    const wchar_t *args[] = { rad, L"BinkMix", video, audio, output, L"/L4", L"/O" };
    for (size_t i = 0; i < ARRAYSIZE(args); i++)
        ev_cmdline_add(cmdline, args[i]);
}

bool ev_rad_parse_title(const wchar_t *title, int *percent)
{
    static const wchar_t done[] = L" - Done!";
    size_t length = wcslen(title);
    if (length >= ARRAYSIZE(done) - 1 && wcscmp(title + length - (ARRAYSIZE(done) - 1), done) == 0) {
        *percent = -1;
        return true;
    }

    wchar_t *end;
    long value = wcstol(title, &end, 10);
    *percent = end > title && *end == L'%' && value >= 0 && value <= 100 ? (int)value : -1;
    return false;
}

static void append(wchar_t *buffer, size_t size, const wchar_t *text)
{
    size_t used = wcslen(buffer);
    if (used > 0 && used + 1 < size)
        buffer[used++] = L' ';
    buffer[used] = L'\0';
    wcsncat(buffer, text, size - used - 1);
}

static BOOL CALLBACK collect_text(HWND child, LPARAM param)
{
    watch_state *state = (watch_state *)param;
    wchar_t class_name[16], text[512];
    DWORD_PTR length;
    if (GetClassNameW(child, class_name, ARRAYSIZE(class_name)) && _wcsicmp(class_name, L"Static") == 0 &&
        SendMessageTimeoutW(child, WM_GETTEXT, ARRAYSIZE(text), (LPARAM)text, SMTO_ABORTIFHUNG, 1000, &length) &&
        length > 0)
        append(state->message, state->message_size, text);
    return TRUE;
}

static BOOL CALLBACK watch_window(HWND window, LPARAM param)
{
    watch_state *state = (watch_state *)param;
    DWORD pid;
    wchar_t class_name[16];
    GetWindowThreadProcessId(window, &pid);
    if (pid != state->pid || !GetClassNameW(window, class_name, ARRAYSIZE(class_name)))
        return TRUE;

    if (wcscmp(class_name, L"RADClass") == 0) {
        wchar_t title[128];
        GetWindowTextW(window, title, ARRAYSIZE(title));
        int percent;
        if (ev_rad_parse_title(title, &percent)) {
            state->done = true;
            PostMessageW(window, WM_CLOSE, 0, 0);
        } else if (percent >= 0) {
            state->percent = percent;
        }
    } else if (wcscmp(class_name, L"#32770") == 0) {
        state->message[0] = L'\0';
        EnumChildWindows(window, collect_text, param);
        PostMessageW(window, WM_CLOSE, 0, 0);
    }
    return TRUE;
}

static void describe_error(wchar_t *message, size_t size, const wchar_t *what, DWORD error)
{
    int used = swprintf(message, size, L"%ls: ", what);
    if (used < 0 || !FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error, 0,
                                    message + used, (DWORD)(size - used), NULL)) {
        swprintf(message, size, L"%ls (error %lu).", what, error);
        return;
    }

    size_t length = wcslen(message);
    while (length > 0 && (message[length - 1] == L'\r' || message[length - 1] == L'\n' || message[length - 1] == L' '))
        message[--length] = L'\0';
}

static bool run(HANDLE job, ev_cmdline *cmdline, const wchar_t *tool, const wchar_t *output,
                ev_rad_progress on_progress, void *context, wchar_t *message, size_t message_size)
{
    message[0] = L'\0';
    HANDLE process;
    if (!ev_process_start(job, cmdline, &process)) {
        describe_error(message, message_size, L"Could not start RAD Video Tools", GetLastError());
        return false;
    }

    watch_state state = {
        .pid = GetProcessId(process),
        .percent = -1,
        .message = message,
        .message_size = message_size,
    };
    int reported = -1;
    while (WaitForSingleObject(process, 100) == WAIT_TIMEOUT) {
        EnumWindows(watch_window, (LPARAM)&state);
        if (on_progress && state.percent != reported) {
            reported = state.percent;
            on_progress(reported, context);
        }
    }

    DWORD exit_code = 1;
    GetExitCodeProcess(process, &exit_code);
    CloseHandle(process);
    if (state.done && exit_code == 0 && is_file(output)) {
        message[0] = L'\0';
        return true;
    }
    if (message[0] == L'\0')
        swprintf(message, message_size, L"%ls exited unexpectedly (code %lu).", tool, exit_code);
    return false;
}

bool ev_rad_compress(HANDLE job, const wchar_t *rad, const wchar_t *folder, int frames, ev_rad_progress on_progress,
                     void *context, wchar_t *message, size_t message_size)
{
    ev_cmdline cmdline = { 0 };
    ev_rad_binkc_args(&cmdline, rad, folder, frames);
    wchar_t output[MAX_PATH + 16];
    swprintf(output, ARRAYSIZE(output), L"%ls\\video.bik", folder);
    return run(job, &cmdline, L"Binkc", output, on_progress, context, message, message_size);
}

bool ev_rad_mix(HANDLE job, const wchar_t *rad, const wchar_t *folder, wchar_t *message, size_t message_size)
{
    ev_cmdline cmdline = { 0 };
    ev_rad_binkmix_args(&cmdline, rad, folder);
    wchar_t output[MAX_PATH + 16];
    swprintf(output, ARRAYSIZE(output), L"%ls\\final.bik", folder);
    return run(job, &cmdline, L"BinkMix", output, NULL, NULL, message, message_size);
}
