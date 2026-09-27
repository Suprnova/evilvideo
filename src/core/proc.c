#include "proc.h"

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef struct buffer {
    char *data;
    size_t length;
    size_t capacity;
} buffer;

typedef struct drain {
    HANDLE pipe;
    buffer text;
    bool ok;
    DWORD error;
} drain;

static void put(ev_cmdline *cmdline, wchar_t c, size_t count)
{
    if (cmdline->length + count >= EV_CMDLINE_MAX) {
        cmdline->too_long = true;
        return;
    }
    wmemset(cmdline->text + cmdline->length, c, count);
    cmdline->length += count;
}

void ev_cmdline_add(ev_cmdline *cmdline, const wchar_t *arg)
{
    if (cmdline->too_long)
        return;

    size_t start = cmdline->length;
    if (start > 0)
        put(cmdline, L' ', 1);

    if (*arg != L'\0' && !wcspbrk(arg, L" \t\n\v\"")) {
        for (const wchar_t *c = arg; *c != L'\0'; c++)
            put(cmdline, *c, 1);
    } else {
        put(cmdline, L'"', 1);
        for (const wchar_t *c = arg;; c++) {
            size_t backslashes = 0;
            for (; *c == L'\\'; c++)
                backslashes++;

            if (*c == L'\0') {
                put(cmdline, L'\\', backslashes * 2);
                break;
            }
            put(cmdline, L'\\', *c == L'"' ? backslashes * 2 + 1 : backslashes);
            put(cmdline, *c, 1);
        }
        put(cmdline, L'"', 1);
    }

    if (cmdline->too_long)
        cmdline->length = start;
    cmdline->text[cmdline->length] = L'\0';
}

static void close_keeping_error(HANDLE handle)
{
    DWORD error = GetLastError();
    CloseHandle(handle);
    SetLastError(error);
}

HANDLE ev_job_create(void)
{
    HANDLE job = CreateJobObjectW(NULL, NULL);
    if (!job)
        return NULL;

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {
        .BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE,
    };
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits)) {
        close_keeping_error(job);
        return NULL;
    }
    return job;
}

static bool spawn(HANDLE job, ev_cmdline *cmdline, bool visible, HANDLE output, HANDLE errors, HANDLE *process)
{
    if (cmdline->too_long) {
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return false;
    }

    bool redirect = output != NULL;
    STARTUPINFOW startup = {
        .cb = sizeof startup,
        .dwFlags = STARTF_USESHOWWINDOW | (redirect ? STARTF_USESTDHANDLES : 0),
        .wShowWindow = visible ? SW_SHOWNOACTIVATE : SW_HIDE,
        .hStdOutput = output,
        .hStdError = errors,
    };
    PROCESS_INFORMATION info;
    if (!CreateProcessW(NULL, cmdline->text, NULL, NULL, redirect, CREATE_SUSPENDED | CREATE_NO_WINDOW, NULL, NULL,
                        &startup, &info))
        return false;

    if (!AssignProcessToJobObject(job, info.hProcess)) {
        DWORD error = GetLastError();
        TerminateProcess(info.hProcess, 1);
        CloseHandle(info.hThread);
        CloseHandle(info.hProcess);
        SetLastError(error);
        return false;
    }

    ResumeThread(info.hThread);
    CloseHandle(info.hThread);
    *process = info.hProcess;
    return true;
}

bool ev_process_start(HANDLE job, ev_cmdline *cmdline, bool visible, HANDLE *process)
{
    return spawn(job, cmdline, visible, NULL, NULL, process);
}

static bool create_pipe(HANDLE *read, HANDLE *write)
{
    SECURITY_ATTRIBUTES inheritable = { .nLength = sizeof inheritable, .bInheritHandle = TRUE };
    if (!CreatePipe(read, write, &inheritable, 0))
        return false;
    if (SetHandleInformation(*read, HANDLE_FLAG_INHERIT, 0))
        return true;

    close_keeping_error(*read);
    close_keeping_error(*write);
    return false;
}

static bool buffer_append(buffer *buffer, const char *bytes, size_t count)
{
    size_t needed = buffer->length + count + 1;
    if (needed > buffer->capacity) {
        size_t capacity = buffer->capacity * 2 > needed ? buffer->capacity * 2 : needed;
        char *data = realloc(buffer->data, capacity);
        if (!data)
            return false;
        buffer->data = data;
        buffer->capacity = capacity;
    }
    memcpy(buffer->data + buffer->length, bytes, count);
    buffer->length += count;
    buffer->data[buffer->length] = '\0';
    return true;
}

static void emit_lines(buffer *text, ev_line_callback on_line, void *context)
{
    char *start = text->data;
    char *end;
    while ((end = memchr(start, '\n', text->length - (size_t)(start - text->data)))) {
        *end = '\0';
        if (end > start && end[-1] == '\r')
            end[-1] = '\0';
        on_line(start, context);
        start = end + 1;
    }

    text->length -= (size_t)(start - text->data);
    memmove(text->data, start, text->length + 1);
}

// Keeps reading after running out of memory, so the child never blocks on a full pipe.
static bool read_pipe(HANDLE pipe, buffer *text, ev_line_callback on_line, void *context)
{
    bool stored = true;
    char chunk[4096];
    DWORD read;
    while (ReadFile(pipe, chunk, sizeof chunk, &read, NULL)) {
        stored = stored && buffer_append(text, chunk, read);
        if (stored && on_line)
            emit_lines(text, on_line, context);
    }

    if (GetLastError() != ERROR_BROKEN_PIPE)
        return false;
    if (!stored) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
    if (on_line && text->length > 0)
        on_line(text->data, context);
    return true;
}

static DWORD WINAPI drain_thread(void *param)
{
    drain *errors = param;
    errors->ok = read_pipe(errors->pipe, &errors->text, NULL, NULL);
    errors->error = GetLastError();
    return 0;
}

bool ev_process_run(HANDLE job, ev_cmdline *cmdline, ev_line_callback on_line, void *context, char **errors,
                    DWORD *exit_code)
{
    *errors = NULL;

    HANDLE output_read, output_write, errors_write;
    drain errors_drain = { 0 };
    if (!create_pipe(&output_read, &output_write))
        return false;
    if (!create_pipe(&errors_drain.pipe, &errors_write)) {
        close_keeping_error(output_read);
        close_keeping_error(output_write);
        return false;
    }

    HANDLE thread = CreateThread(NULL, 0, drain_thread, &errors_drain, 0, NULL);
    HANDLE process = NULL;
    bool ok = thread && spawn(job, cmdline, false, output_write, errors_write, &process);
    DWORD error = GetLastError();

    // Once only the child holds the write ends, the reads end when it exits.
    CloseHandle(output_write);
    CloseHandle(errors_write);

    buffer output = { 0 };
    if (ok) {
        ok = read_pipe(output_read, &output, on_line, context);
        error = GetLastError();
    }
    if (!ok && process)
        TerminateProcess(process, 1);

    if (thread) {
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }
    if (process) {
        WaitForSingleObject(process, INFINITE);
        GetExitCodeProcess(process, exit_code);
        CloseHandle(process);
    }
    CloseHandle(output_read);
    CloseHandle(errors_drain.pipe);
    free(output.data);

    if (ok && !errors_drain.ok) {
        ok = false;
        error = errors_drain.error;
    }
    if (ok && !buffer_append(&errors_drain.text, "", 0)) {
        ok = false;
        error = ERROR_NOT_ENOUGH_MEMORY;
    }

    if (ok)
        *errors = errors_drain.text.data;
    else
        free(errors_drain.text.data);
    SetLastError(error);
    return ok;
}
