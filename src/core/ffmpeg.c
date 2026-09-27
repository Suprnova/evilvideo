#include "ffmpeg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define ADD_ARGS(cmdline, ...) add_args(cmdline, (const wchar_t *[]){ __VA_ARGS__, NULL })

static void add_args(ev_cmdline *cmdline, const wchar_t *const *args)
{
    for (; *args; args++)
        ev_cmdline_add(cmdline, *args);
}

bool ev_ffmpeg_locate(wchar_t path[MAX_PATH])
{
    static const wchar_t file_name[] = L"ffmpeg.exe";

    DWORD length = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (length == 0 || length == MAX_PATH)
        return false;

    wchar_t *name = wcsrchr(path, L'\\') + 1;
    if ((size_t)(name - path) + ARRAYSIZE(file_name) > MAX_PATH) {
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return false;
    }
    wmemcpy(name, file_name, ARRAYSIZE(file_name));

    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return false;
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return false;
    }
    return true;
}

void ev_ffmpeg_probe_args(ev_cmdline *cmdline, const wchar_t *ffmpeg, const wchar_t *input)
{
    ADD_ARGS(cmdline, ffmpeg, L"-hide_banner", L"-nostdin", L"-i", input);
}

// The frame pattern is a printf format for ffmpeg, so a literal % in the folder must be doubled.
static void frames_pattern(wchar_t *pattern, const wchar_t *folder)
{
    for (; *folder; folder++) {
        if (*folder == L'%')
            *pattern++ = L'%';
        *pattern++ = *folder;
    }
    wcscpy(pattern, L"\\frames\\f%06d.jpg");
}

void ev_ffmpeg_prepare_args(ev_cmdline *cmdline, const wchar_t *ffmpeg, const ev_prepare *prepare)
{
    if (wcslen(prepare->folder) >= MAX_PATH) {
        cmdline->too_long = true;
        return;
    }

    ADD_ARGS(cmdline, ffmpeg, L"-hide_banner", L"-nostdin", L"-y", L"-loglevel", L"error", L"-progress", L"pipe:1",
             L"-nostats");

    if (prepare->trim) {
        wchar_t start[32], length[32];
        swprintf(start, ARRAYSIZE(start), L"%.3f", prepare->trim_start);
        swprintf(length, ARRAYSIZE(length), L"%.3f", prepare->trim_end - prepare->trim_start);
        ADD_ARGS(cmdline, L"-ss", start, L"-t", length);
    }

    int width = prepare->game->width;
    int height = prepare->game->height;
    wchar_t filter[512];
    if (prepare->letterbox)
        swprintf(filter, ARRAYSIZE(filter),
                 L"fps=30000/1001,"
                 L"scale=w='trunc(%d*min(1,dar*3/4)/2)*2':h='trunc(%d*min(1,4/(3*dar))/2)*2'"
                 L":flags=lanczos:out_range=full,"
                 L"setsar=1,pad=%d:%d:(ow-iw)/2:(oh-ih)/2:black,format=yuv420p",
                 width, height, width, height);
    else
        swprintf(filter, ARRAYSIZE(filter),
                 L"fps=30000/1001,scale=%d:%d:flags=lanczos:out_range=full,setsar=1,format=yuv420p", width, height);

    wchar_t frames[2 * MAX_PATH + 32];
    frames_pattern(frames, prepare->folder);
    ADD_ARGS(cmdline, L"-i", prepare->input, L"-map", L"0:v:0", L"-vf", filter, L"-q:v", L"2", L"-start_number", L"1",
             frames);

    if (prepare->audio) {
        wchar_t audio[MAX_PATH + 16];
        swprintf(audio, ARRAYSIZE(audio), L"%ls\\audio.wav", prepare->folder);
        ADD_ARGS(cmdline, L"-map", L"0:a:0", L"-ac", L"2", L"-ar", L"48000", L"-c:a", L"pcm_s16le", audio);
    }
}

static const char *next_line(const char *line)
{
    const char *end = strchr(line, '\n');
    return end ? end + 1 : NULL;
}

static bool line_contains(const char *line, const char *text)
{
    const char *found = strstr(line, text);
    const char *end = strchr(line, '\n');
    return found && (!end || found < end);
}

bool ev_ffmpeg_parse_probe(const char *errors, ev_probe *probe)
{
    const char *input = strstr(errors, "Input #0");
    if (!input)
        return false;

    probe->duration = -1;
    probe->has_audio = false;
    for (const char *line = input; line; line = next_line(line)) {
        line += strspn(line, " ");
        int hours, minutes;
        double seconds;
        if (sscanf(line, "Duration: %d:%d:%lf", &hours, &minutes, &seconds) == 3)
            probe->duration = hours * 3600.0 + minutes * 60.0 + seconds;
        else if (strncmp(line, "Stream #", 8) == 0 && line_contains(line, ": Audio:"))
            probe->has_audio = true;
    }
    return true;
}

bool ev_ffmpeg_parse_progress(const char *line, double *seconds)
{
    static const char key[] = "out_time_us=";
    if (strncmp(line, key, ARRAYSIZE(key) - 1) != 0)
        return false;

    const char *value = line + ARRAYSIZE(key) - 1;
    char *end;
    long long microseconds = strtoll(value, &end, 10);
    if (end == value || *end != '\0' || microseconds < 0)
        return false;

    *seconds = microseconds / 1e6;
    return true;
}

const char *ev_ffmpeg_last_lines(const char *errors, int count)
{
    const char *start = errors + strlen(errors);
    while (start > errors && (start[-1] == '\n' || start[-1] == '\r'))
        start--;

    for (int breaks = 0; start > errors; start--) {
        if (start[-1] == '\n' && ++breaks == count)
            break;
    }
    return start;
}
