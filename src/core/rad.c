#include "rad.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

typedef struct seen_window {
    HWND window;
    wchar_t title[128];
} seen_window;

typedef struct dialog_text {
    wchar_t text[512];
} dialog_text;

typedef struct watch_state {
    const ev_rad_run *run;
    const wchar_t *tool;
    DWORD pid;
    bool done;
    int percent;
    wchar_t *message;
    size_t message_size;
    seen_window seen[16];
    size_t seen_count;
    dialog_text logged_dialog;
    int empty_dialog_polls;
} watch_state;

// How many polls (100 ms each) a dialog may show no text before it is closed anyway.
#define EMPTY_DIALOG_POLLS 20

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
        wchar_t *name = NULL;
        DWORD length = GetFullPathNameW(given, MAX_PATH, path, &name);
        bool named_right = name && _wcsicmp(name, L"radvideo64.exe") == 0;
        if (length > 0 && length < MAX_PATH && named_right && is_file(path))
            return true;
        if (explicit_path) {
            SetLastError(named_right || !is_file(path) ? ERROR_FILE_NOT_FOUND : ERROR_BAD_EXE_FORMAT);
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
    dialog_text *dialog = (dialog_text *)param;
    wchar_t class_name[16], text[512];
    DWORD_PTR length;
    if (GetClassNameW(child, class_name, ARRAYSIZE(class_name)) && _wcsicmp(class_name, L"Static") == 0 &&
        SendMessageTimeoutW(child, WM_GETTEXT, ARRAYSIZE(text), (LPARAM)text, SMTO_ABORTIFHUNG, 1000, &length) &&
        length > 0)
        append(dialog->text, ARRAYSIZE(dialog->text), text);
    return TRUE;
}

static void log_line(const watch_state *state, const wchar_t *format, ...)
{
    if (!state->run->on_log)
        return;

    wchar_t line[2048];
    int prefix = swprintf(line, ARRAYSIZE(line), L"%ls: ", state->tool);
    va_list args;
    va_start(args, format);
    int length = vswprintf(line + prefix, ARRAYSIZE(line) - prefix, format, args);
    va_end(args);
    if (length >= 0)
        state->run->on_log(line, state->run->context);
}

// Logs a window the first time it is seen, and again whenever its title changes. The input method windows Windows
// and Wine give every GUI program are left out.
static void note_window(watch_state *state, HWND window, const wchar_t *class_name, const wchar_t *title)
{
    if (!state->run->on_log || wcscmp(class_name, L"IME") == 0 || wcscmp(class_name, L"MSCTFIME UI") == 0 ||
        wcscmp(class_name, L"Wine IME") == 0)
        return;

    seen_window *seen = NULL;
    for (size_t i = 0; i < state->seen_count && !seen; i++) {
        if (state->seen[i].window == window)
            seen = &state->seen[i];
    }
    if (seen && wcscmp(seen->title, title) == 0)
        return;
    if (!seen && state->seen_count < ARRAYSIZE(state->seen))
        seen = &state->seen[state->seen_count++];
    if (seen) {
        seen->window = window;
        wcscpy(seen->title, title);
    }
    log_line(state, L"%ls window \"%ls\"", class_name, title);
}

static BOOL CALLBACK watch_window(HWND window, LPARAM param)
{
    watch_state *state = (watch_state *)param;
    DWORD pid;
    wchar_t class_name[64], title[128];
    GetWindowThreadProcessId(window, &pid);
    if (pid != state->pid || !GetClassNameW(window, class_name, ARRAYSIZE(class_name)))
        return TRUE;
    GetWindowTextW(window, title, ARRAYSIZE(title));
    note_window(state, window, class_name, title);

    if (wcscmp(class_name, L"RADClass") == 0) {
        int percent;
        if (ev_rad_parse_title(title, &percent)) {
            if (!state->done)
                log_line(state, L"finished; closing its window");
            state->done = true;
            PostMessageW(window, WM_CLOSE, 0, 0);
        } else if (percent >= 0) {
            state->percent = percent;
        }
    } else if (wcscmp(class_name, L"#32770") == 0) {
        // A new dialog can be seen before its text is set, and one still closing from an earlier poll can have lost
        // it, so an empty read keeps the last message and leaves the dialog open until it has stayed empty a while.
        dialog_text dialog = { 0 };
        EnumChildWindows(window, collect_text, (LPARAM)&dialog);
        if (dialog.text[0] != L'\0') {
            state->empty_dialog_polls = 0;
            state->message[0] = L'\0';
            append(state->message, state->message_size, dialog.text);
            if (wcscmp(dialog.text, state->logged_dialog.text) != 0) {
                log_line(state, L"dialog says \"%ls\"; closing it", dialog.text);
                state->logged_dialog = dialog;
            }
        } else if (++state->empty_dialog_polls < EMPTY_DIALOG_POLLS) {
            return TRUE;
        }
        PostMessageW(window, WM_CLOSE, 0, 0);
    }
    return TRUE;
}

static bool run_tool(const ev_rad_run *run, ev_cmdline *cmdline, const wchar_t *tool, const wchar_t *output,
                     wchar_t *message, size_t message_size)
{
    watch_state state = {
        .run = run,
        .tool = tool,
        .percent = -1,
        .message = message,
        .message_size = message_size,
    };
    message[0] = L'\0';
    log_line(&state, L"running %ls", cmdline->text);

    ULONGLONG start = GetTickCount64();
    HANDLE process;
    if (!ev_process_start(run->job, cmdline, run->visible, &process)) {
        ev_error_message(message, message_size, L"Could not start RAD Video Tools", GetLastError());
        return false;
    }

    state.pid = GetProcessId(process);
    int reported = -1;
    while (WaitForSingleObject(process, 100) == WAIT_TIMEOUT) {
        EnumWindows(watch_window, (LPARAM)&state);
        if (run->on_progress && state.percent != reported) {
            reported = state.percent;
            run->on_progress(reported, run->context);
        }
    }

    DWORD exit_code = 1;
    GetExitCodeProcess(process, &exit_code);
    CloseHandle(process);
    log_line(&state, L"exited with code %lu after %.1f s", exit_code, (GetTickCount64() - start) / 1000.0);
    if (state.done && exit_code == 0 && is_file(output)) {
        message[0] = L'\0';
        return true;
    }
    if (message[0] == L'\0')
        swprintf(message, message_size, L"%ls exited unexpectedly (code %lu).", tool, exit_code);
    return false;
}

bool ev_rad_compress(const ev_rad_run *run, int frames, wchar_t *message, size_t message_size)
{
    ev_cmdline cmdline = { 0 };
    ev_rad_binkc_args(&cmdline, run->rad, run->folder, frames);
    wchar_t output[MAX_PATH + 16];
    swprintf(output, ARRAYSIZE(output), L"%ls\\video.bik", run->folder);
    return run_tool(run, &cmdline, L"Binkc", output, message, message_size);
}

bool ev_rad_mix(const ev_rad_run *run, wchar_t *message, size_t message_size)
{
    ev_cmdline cmdline = { 0 };
    ev_rad_binkmix_args(&cmdline, run->rad, run->folder);
    wchar_t output[MAX_PATH + 16];
    swprintf(output, ARRAYSIZE(output), L"%ls\\final.bik", run->folder);
    return run_tool(run, &cmdline, L"BinkMix", output, message, message_size);
}
