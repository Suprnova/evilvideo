#include "convert.h"

#include "ffmpeg.h"
#include "files.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef struct convert_state {
    const ev_conversion *conversion;
    ev_job *job;
    const wchar_t *folder;
    wchar_t *message;
    size_t message_size;
    double duration;
    ev_stage stage;
    int percent;
} convert_state;

static void report(convert_state *state, ev_stage stage, int percent)
{
    if (stage == state->stage && percent == state->percent)
        return;
    state->stage = stage;
    state->percent = percent;
    if (state->conversion->on_progress)
        state->conversion->on_progress(stage, percent, state->conversion->context);
}

static void log_text(const convert_state *state, const wchar_t *source, const wchar_t *lead, const wchar_t *text)
{
    if (!state->conversion->on_log)
        return;

    size_t size = wcslen(source) + wcslen(lead) + wcslen(text) + 3;
    wchar_t *line = malloc(size * sizeof *line);
    if (!line)
        return;
    swprintf(line, size, L"%ls: %ls%ls", source, lead, text);
    state->conversion->on_log(line, state->conversion->context);
    free(line);
}

static void log_format(const convert_state *state, const wchar_t *source, const wchar_t *format, ...)
{
    if (!state->conversion->on_log)
        return;

    wchar_t text[1024];
    va_list args;
    va_start(args, format);
    int length = vswprintf(text, ARRAYSIZE(text), format, args);
    va_end(args);
    if (length >= 0)
        log_text(state, source, L"", text);
}

static void forward_log(const wchar_t *line, void *context)
{
    const convert_state *state = context;
    state->conversion->on_log(line, state->conversion->context);
}

static wchar_t *widen(const char *text, size_t length)
{
    int size = length > 0 ? MultiByteToWideChar(CP_UTF8, 0, text, (int)length, NULL, 0) : 0;
    wchar_t *wide = malloc(((size_t)size + 1) * sizeof *wide);
    if (!wide)
        return NULL;
    if (size > 0)
        MultiByteToWideChar(CP_UTF8, 0, text, (int)length, wide, size);
    wide[size] = L'\0';
    return wide;
}

static void log_output(const convert_state *state, const char *output)
{
    if (!state->conversion->on_log)
        return;

    for (const char *line = output; *line;) {
        size_t length = strcspn(line, "\r\n");
        wchar_t *wide = length > 0 ? widen(line, length) : NULL;
        if (wide) {
            log_text(state, L"ffmpeg", L"", wide);
            free(wide);
        }
        line += length;
        line += strspn(line, "\r\n");
    }
}

static bool fail(convert_state *state, const wchar_t *format, ...)
{
    wchar_t text[2048];
    va_list args;
    va_start(args, format);
    if (vswprintf(text, ARRAYSIZE(text), format, args) < 0)
        text[0] = L'\0';
    va_end(args);

    state->message[0] = L'\0';
    wcsncat(state->message, text, state->message_size - 1);
    return false;
}

static bool fail_system(convert_state *state, const wchar_t *what, DWORD error)
{
    ev_error_message(state->message, state->message_size, what, error);
    return false;
}

// Joins the last lines of ffmpeg's standard error into one line for a message. Free it with free.
static wchar_t *ffmpeg_reason(const char *errors, int lines)
{
    const char *last = ev_ffmpeg_last_lines(errors, lines);
    wchar_t *wide = widen(last, strlen(last));
    if (!wide)
        return NULL;

    wchar_t *reason = malloc((2 * wcslen(wide) + 1) * sizeof *reason);
    if (reason) {
        wchar_t *end = reason;
        for (const wchar_t *line = wide; *line;) {
            size_t length = wcscspn(line, L"\r\n");
            if (length > 0) {
                if (end > reason) {
                    *end++ = L';';
                    *end++ = L' ';
                }
                wmemcpy(end, line, length);
                end += length;
            }
            line += length;
            line += wcsspn(line, L"\r\n");
        }
        *end = L'\0';
    }
    free(wide);
    return reason;
}

static bool fail_ffmpeg(convert_state *state, const wchar_t *what, const char *errors, int lines, DWORD exit_code)
{
    wchar_t *reason = ffmpeg_reason(errors, lines);
    if (reason && reason[0] != L'\0')
        fail(state, L"%ls: %.1000ls", what, reason);
    else
        fail(state, L"%ls (ffmpeg exited with code %lu).", what, exit_code);
    free(reason);
    return false;
}

static bool run_ffmpeg(convert_state *state, ev_cmdline *cmdline, ev_line_callback on_line, char **errors,
                       DWORD *exit_code)
{
    log_text(state, L"ffmpeg", L"running ", cmdline->text);
    ULONGLONG start = GetTickCount64();
    if (!ev_process_run(state->job, cmdline, on_line, state, errors, exit_code))
        return fail_system(state, L"Could not run ffmpeg", GetLastError());

    log_output(state, *errors);
    log_format(state, L"ffmpeg", L"exited with code %lu after %.1f s", *exit_code,
               (GetTickCount64() - start) / 1000.0);
    return true;
}

static bool run_probe(convert_state *state, ev_probe *probe)
{
    const ev_conversion *conversion = state->conversion;
    report(state, EV_STAGE_PROBE, 0);

    ev_cmdline cmdline = { 0 };
    ev_ffmpeg_probe_args(&cmdline, conversion->ffmpeg, conversion->input);
    char *errors;
    DWORD exit_code;
    if (!run_ffmpeg(state, &cmdline, NULL, &errors, &exit_code))
        return false;
    bool opened = ev_ffmpeg_parse_probe(errors, probe) ||
                  fail_ffmpeg(state, L"ffmpeg could not read the input", errors, 1, exit_code);
    free(errors);
    if (!opened)
        return false;

    const wchar_t *audio = probe->has_audio ? L"audio" : L"no audio";
    if (probe->duration >= 0)
        log_format(state, L"evilvideo", L"the input lasts %.2f s and has %ls", probe->duration, audio);
    else
        log_format(state, L"evilvideo", L"the input's length is unknown; it has %ls", audio);

    state->duration = probe->duration;
    if (conversion->trim) {
        if (probe->duration >= 0 && conversion->trim_start >= probe->duration)
            return fail(state, L"The trim starts at %g s, but the input is only %g s long.", conversion->trim_start,
                        probe->duration);
        bool past_end = probe->duration >= 0 && conversion->trim_end > probe->duration;
        state->duration = (past_end ? probe->duration : conversion->trim_end) - conversion->trim_start;
    }
    return true;
}

static void on_prepare_line(const char *line, void *context)
{
    convert_state *state = context;
    double seconds;
    if (state->duration > 0 && ev_ffmpeg_parse_progress(line, &seconds)) {
        int percent = (int)(50 * seconds / state->duration);
        report(state, EV_STAGE_PREPARE, percent < 50 ? percent : 50);
    }
}

static bool run_prepare(convert_state *state, const ev_probe *probe)
{
    const ev_conversion *conversion = state->conversion;
    report(state, EV_STAGE_PREPARE, state->duration > 0 ? 0 : -1);

    ev_prepare prepare = {
        .input = conversion->input,
        .folder = state->folder,
        .game = conversion->game,
        .letterbox = conversion->letterbox,
        .trim = conversion->trim,
        .trim_start = conversion->trim_start,
        .trim_end = conversion->trim_end,
        .audio = probe->has_audio,
    };
    ev_cmdline cmdline = { 0 };
    ev_ffmpeg_prepare_args(&cmdline, conversion->ffmpeg, &prepare);
    char *errors;
    DWORD exit_code;
    if (!run_ffmpeg(state, &cmdline, on_prepare_line, &errors, &exit_code))
        return false;
    bool prepared = exit_code == 0 || fail_ffmpeg(state, L"ffmpeg could not convert the input", errors, 3, exit_code);
    free(errors);
    return prepared;
}

static void on_compress_progress(int percent, void *context)
{
    report(context, EV_STAGE_COMPRESS, 50 + percent * 48 / 100);
}

static bool run_stages(convert_state *state)
{
    const ev_conversion *conversion = state->conversion;
    ev_probe probe;
    if (!run_probe(state, &probe) || !run_prepare(state, &probe))
        return false;

    int frames = ev_frames_count(state->folder);
    log_format(state, L"evilvideo", L"ffmpeg wrote %d frames", frames);
    if (frames <= 0)
        return fail(state, conversion->trim ? L"ffmpeg wrote no frames; the trim may start past the end of the input."
                                            : L"ffmpeg wrote no frames.");

    ev_rad_run run = {
        .job = state->job,
        .rad = conversion->rad,
        .folder = state->folder,
        .visible = conversion->show_rad,
        .on_progress = on_compress_progress,
        .on_log = conversion->on_log ? forward_log : NULL,
        .context = state,
    };
    report(state, EV_STAGE_COMPRESS, 50);
    if (!ev_rad_compress(&run, frames, state->message, state->message_size))
        return false;
    if (probe.has_audio) {
        report(state, EV_STAGE_MIX, 98);
        if (!ev_rad_mix(&run, state->message, state->message_size))
            return false;
    }

    report(state, EV_STAGE_FINISH, 99);
    wchar_t result[MAX_PATH + 16];
    swprintf(result, ARRAYSIZE(result), L"%ls\\%ls", state->folder, probe.has_audio ? L"final.bik" : L"video.bik");
    if (!MoveFileExW(result, conversion->output, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
        return fail_system(state, L"Could not save the output", GetLastError());
    log_text(state, L"evilvideo", L"saved ", conversion->output);
    report(state, EV_STAGE_FINISH, 100);
    return true;
}

ev_result ev_convert(const ev_conversion *conversion, ev_job *job, wchar_t *message, size_t message_size)
{
    convert_state state = {
        .conversion = conversion,
        .job = job,
        .message = message,
        .message_size = message_size,
        .duration = -1,
        .stage = EV_STAGE_PROBE,
        .percent = -2,
    };
    message[0] = L'\0';
    log_text(&state, L"evilvideo", L"ffmpeg is ", conversion->ffmpeg);
    log_text(&state, L"evilvideo", L"RAD Video Tools are ", conversion->rad);

    wchar_t folder[MAX_PATH];
    bool converted;
    if (ev_temp_create(folder)) {
        state.folder = folder;
        log_text(&state, L"evilvideo", L"temporary folder is ", folder);
        converted = run_stages(&state);
        if (ev_temp_delete(folder))
            log_text(&state, L"evilvideo", L"deleted the temporary folder", L"");
        else
            log_text(&state, L"evilvideo", L"could not delete the temporary folder ", folder);
    } else {
        converted = fail_system(&state, L"Could not create a temporary folder", GetLastError());
    }

    if (converted)
        return EV_RESULT_CONVERTED;
    if (ev_job_cancelled(job)) {
        message[0] = L'\0';
        return EV_RESULT_CANCELLED;
    }
    return EV_RESULT_FAILED;
}
