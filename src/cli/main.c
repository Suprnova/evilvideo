#include "args.h"
#include "convert.h"
#include "ffmpeg.h"
#include "files.h"
#include "version.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

static const wchar_t help[] =
    L"Usage: evilvideo-cli [options] <input>...\n"
    L"\n"
    L"Converts videos to Bink for Heavy Iron Studios' EvilEngine games.\n"
    L"\n"
    L"  -g, --game <id>        target game: n100f, bfbb, tssm, incredibles, rotu (required)\n"
    L"  -o, --output <path>    output file (one input) or folder (several inputs);\n"
    L"                         default: next to each input, with a .bik extension\n"
    L"      --stretch          stretch to fill the frame instead of letterboxing\n"
    L"      --trim <from>-<to> keep only this range, in seconds (e.g. 1.5-12)\n"
    L"  -y, --overwrite        replace existing outputs instead of skipping them\n"
    L"      --rad <path>       radvideo64.exe to use (default: the standard install)\n"
    L"      --show-rad         show the RAD Video Tools windows instead of hiding them\n"
    L"  -v, --verbose          also print what each step runs and what the tools report\n"
    L"  -q, --quiet            print only errors and the summary\n"
    L"  -h, --help             show this help\n"
    L"      --version          show the version\n";

static const wchar_t *const stage_names[] = { L"reading", L"preparing", L"compressing", L"mixing audio", L"saving" };

typedef struct stream {
    HANDLE handle;
    bool console;
} stream;

typedef enum outcome { CONVERTED, SKIPPED, FAILED, CANCELLED } outcome;

typedef struct file {
    wchar_t *input;
    /** Why the file failed, or NULL. */
    wchar_t *error;
} file;

typedef struct batch {
    const cli_options *options;
    const wchar_t *ffmpeg;
    const wchar_t *rad;
    bool output_folder;
    file *files;
    int count;
    int index;
    int stage;
    int percent;
    bool progress_drawn;
} batch;

static stream out, err;
static ev_job job;

static stream open_stream(DWORD id)
{
    HANDLE handle = GetStdHandle(id);
    DWORD mode;
    return (stream){ handle, GetConsoleMode(handle, &mode) };
}

static void write_text(const stream *stream, const wchar_t *text)
{
    DWORD written;
    int length = (int)wcslen(text);
    if (stream->console) {
        WriteConsoleW(stream->handle, text, length, &written, NULL);
        return;
    }

    int size = WideCharToMultiByte(CP_UTF8, 0, text, length, NULL, 0, NULL, NULL);
    char *utf8 = size > 0 ? malloc(size) : NULL;
    if (utf8) {
        WideCharToMultiByte(CP_UTF8, 0, text, length, utf8, size, NULL, NULL);
        WriteFile(stream->handle, utf8, size, &written, NULL);
        free(utf8);
    }
}

static wchar_t *format_text(const wchar_t *format, va_list args)
{
    va_list measure;
    va_copy(measure, args);
    int length = _vscwprintf(format, measure);
    va_end(measure);

    wchar_t *text = length >= 0 ? malloc((length + 1) * sizeof *text) : NULL;
    if (text)
        vswprintf(text, length + 1, format, args);
    return text;
}

static void print(const stream *stream, const wchar_t *format, ...)
{
    va_list args;
    va_start(args, format);
    wchar_t *text = format_text(format, args);
    va_end(args);
    if (text) {
        write_text(stream, text);
        free(text);
    }
}

static int console_width(void)
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    return GetConsoleScreenBufferInfo(out.handle, &info) && info.dwSize.X > 1 ? info.dwSize.X : 80;
}

// Fills the current console line with text, cut or padded to one column short of the width so the cursor stays on it.
static void draw_line(const wchar_t *text)
{
    int width = console_width() - 1;
    wchar_t *line = malloc((width + 3) * sizeof *line);
    if (line) {
        swprintf(line, width + 3, L"\r%-*.*ls\r", width, width, text);
        write_text(&out, line);
        free(line);
    }
}

static void clear_progress(batch *batch)
{
    if (batch->progress_drawn)
        draw_line(L"");
    batch->progress_drawn = false;
}

static const wchar_t *current_name(const batch *batch)
{
    return cli_file_name(batch->files[batch->index].input);
}

static void draw_progress(batch *batch)
{
    wchar_t percent[8] = L"";
    if (batch->percent >= 0)
        swprintf(percent, ARRAYSIZE(percent), L" %d%%", batch->percent);

    wchar_t line[MAX_PATH + 64];
    swprintf(line, ARRAYSIZE(line), L"[%d/%d] %.*ls \x2014 %ls%ls", batch->index + 1, batch->count, MAX_PATH,
             current_name(batch), stage_names[batch->stage], percent);
    draw_line(line);
    batch->progress_drawn = true;
}

static void on_progress(ev_stage stage, int percent, void *context)
{
    batch *batch = context;
    bool new_stage = (int)stage != batch->stage;
    batch->stage = stage;
    batch->percent = percent;
    if (batch->options->quiet)
        return;

    if (out.console)
        draw_progress(batch);
    else if (new_stage)
        print(&out, L"[%d/%d] %ls \x2014 %ls\n", batch->index + 1, batch->count, current_name(batch),
              stage_names[stage]);
}

static void on_log(const wchar_t *line, void *context)
{
    batch *batch = context;
    bool redraw = batch->progress_drawn;
    clear_progress(batch);
    print(&out, L"%ls\n", line);
    if (redraw)
        draw_progress(batch);
}

static outcome convert_file(batch *batch, file *file, wchar_t *output)
{
    if (!batch->options->overwrite && GetFileAttributesW(output) != INVALID_FILE_ATTRIBUTES)
        return SKIPPED;

    const cli_options *options = batch->options;
    ev_conversion conversion = {
        .input = file->input,
        .output = output,
        .game = options->game,
        .letterbox = options->letterbox,
        .trim = options->trim,
        .trim_start = options->trim_start,
        .trim_end = options->trim_end,
        .ffmpeg = batch->ffmpeg,
        .rad = batch->rad,
        .show_rad = options->show_rad,
        .on_progress = on_progress,
        .on_log = options->verbose ? on_log : NULL,
        .context = batch,
    };
    wchar_t message[2048];
    switch (ev_convert(&conversion, &job, message, ARRAYSIZE(message))) {
    case EV_RESULT_CONVERTED:
        return CONVERTED;
    case EV_RESULT_CANCELLED:
        return CANCELLED;
    default:
        file->error = cli_checked(_wcsdup(message));
        return FAILED;
    }
}

static outcome run_file(batch *batch, file *file)
{
    batch->stage = -1;
    batch->percent = -1;
    wchar_t *output = cli_output_path(file->input, batch->options->output, batch->output_folder);
    outcome outcome = file->error ? FAILED : convert_file(batch, file, output);
    clear_progress(batch);
    if (!batch->options->quiet) {
        print(&out, L"[%d/%d] %ls \x2014 ", batch->index + 1, batch->count, current_name(batch));
        if (outcome == CONVERTED)
            print(&out, L"converted\n");
        else if (outcome == SKIPPED)
            print(&out, L"skipped, %ls exists\n", output);
        else if (outcome == FAILED)
            print(&out, L"failed: %ls\n", file->error);
        else
            print(&out, L"cancelled\n");
    }
    free(output);
    return outcome;
}

static void add_file(batch *batch, wchar_t *input, wchar_t *error)
{
    batch->files = cli_checked(realloc(batch->files, (batch->count + 1) * sizeof *batch->files));
    batch->files[batch->count++] = (file){ input, error };
}

static void add_inputs(batch *batch)
{
    for (int i = 0; i < batch->options->input_count; i++) {
        wchar_t *input = batch->options->inputs[i];
        if (!cli_is_pattern(input)) {
            add_file(batch, cli_checked(_wcsdup(input)), NULL);
            continue;
        }

        int count;
        wchar_t **paths = cli_expand(input, &count);
        if (count == 0)
            add_file(batch, cli_checked(_wcsdup(input)), cli_checked(_wcsdup(L"No file matches this pattern.")));
        for (int j = 0; j < count; j++)
            add_file(batch, paths[j], NULL);
        free(paths);
    }
}

static bool create_output_folder(const wchar_t *folder)
{
    if (!CreateDirectoryW(folder, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        wchar_t message[512];
        ev_error_message(message, ARRAYSIZE(message), L"Could not create the output folder", GetLastError());
        print(&err, L"evilvideo-cli: %ls\n", message);
        return false;
    }
    DWORD attributes = GetFileAttributesW(folder);
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        print(&err, L"evilvideo-cli: %ls is a file, but several inputs need an output folder\n", folder);
        return false;
    }
    return true;
}

static bool locate_tools(const cli_options *options, wchar_t ffmpeg[MAX_PATH], wchar_t rad[MAX_PATH])
{
    if (!ev_ffmpeg_locate(ffmpeg)) {
        print(&err, L"evilvideo-cli: ffmpeg.exe is missing from evilvideo's folder; reinstall evilvideo\n");
        return false;
    }
    if (ev_rad_locate(options->rad, NULL, rad))
        return true;

    if (options->rad)
        print(&err, L"evilvideo-cli: %ls is not a file\n", options->rad);
    else
        print(&err, L"evilvideo-cli: RAD Video Tools are not installed; install them from "
                    L"https://www.radgametools.com/bnkdown.htm, or pass --rad with the path to radvideo64.exe\n");
    return false;
}

static BOOL WINAPI on_console_event(DWORD event)
{
    (void)event;
    ev_job_cancel(&job);
    return TRUE;
}

static int run_batch(batch *batch)
{
    int counts[CANCELLED + 1] = { 0 };
    SetConsoleCtrlHandler(on_console_event, TRUE);
    for (batch->index = 0; batch->index < batch->count && !counts[CANCELLED]; batch->index++)
        counts[run_file(batch, &batch->files[batch->index])]++;
    SetConsoleCtrlHandler(on_console_event, FALSE);

    if (counts[CANCELLED])
        print(&out, L"Cancelled.\n");
    print(&out, L"%d converted, %d skipped, %d failed.\n", counts[CONVERTED], counts[SKIPPED], counts[FAILED]);
    for (int i = 0; i < batch->count; i++) {
        if (batch->files[i].error)
            print(&out, L"  %ls: %ls\n", batch->files[i].input, batch->files[i].error);
    }
    return counts[FAILED] || counts[CANCELLED] ? 1 : 0;
}

int wmain(int argc, wchar_t **argv)
{
    out = open_stream(STD_OUTPUT_HANDLE);
    err = open_stream(STD_ERROR_HANDLE);

    cli_options options;
    wchar_t message[512];
    if (!cli_parse(argc, argv, &options, message, ARRAYSIZE(message))) {
        print(&err, L"evilvideo-cli: %ls\n\n%ls", message, help);
        return 2;
    }
    if (options.help) {
        print(&out, L"%ls", help);
        return 0;
    }
    if (options.version) {
        print(&out, L"evilvideo-cli %ls\n", L"" EV_VERSION_STRING);
        return 0;
    }

    ev_temp_cleanup();
    wchar_t ffmpeg[MAX_PATH], rad[MAX_PATH];
    if (!locate_tools(&options, ffmpeg, rad))
        return 2;

    batch batch = { .options = &options, .ffmpeg = ffmpeg, .rad = rad };
    add_inputs(&batch);
    if (options.output) {
        bool several = options.input_count > 1 || cli_is_pattern(options.inputs[0]);
        batch.output_folder = cli_output_is_folder(options.output, several);
        if (batch.output_folder && !create_output_folder(options.output))
            return 2;
    }

    if (!ev_job_create(&job)) {
        ev_error_message(message, ARRAYSIZE(message), L"Could not create a job object", GetLastError());
        print(&err, L"evilvideo-cli: %ls\n", message);
        return 1;
    }
    int exit_code = run_batch(&batch);
    ev_job_close(&job);

    for (int i = 0; i < batch.count; i++) {
        free(batch.files[i].input);
        free(batch.files[i].error);
    }
    free(batch.files);
    return exit_code;
}
