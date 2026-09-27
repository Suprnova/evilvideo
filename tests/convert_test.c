#include "convert.h"
#include "files.h"
#include "test.h"

#include <shellapi.h>
#include <string.h>
#include <wchar.h>

typedef struct progress_report {
    ev_stage stage;
    int percent;
} progress_report;

typedef struct convert_fixture {
    ev_job job;
    wchar_t self[MAX_PATH];
    wchar_t folder[MAX_PATH];
    wchar_t output[MAX_PATH];
    wchar_t message[512];
    ev_conversion conversion;
    ev_result result;
    progress_report progress[64];
    size_t progress_count;
    wchar_t log[48][512];
    size_t log_count;
    HANDLE preparing;
} convert_fixture;

static convert_fixture fixture;

static void write_to(DWORD handle, const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(handle), text, (DWORD)strlen(text), &written, NULL);
}

// Stands in for ffmpeg. The input's name picks the behavior: "missing" cannot be opened, "silent" has no audio,
// "broken" fails to convert, and "stuck" hangs while converting. Every other input lasts 2 seconds and has audio.
int convert_test_fake_ffmpeg(void)
{
    int argc;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const wchar_t *input = L"", *frames = NULL, *audio = NULL;
    bool prepare = false;
    for (int i = 1; i < argc; i++) {
        if (wcscmp(argv[i], L"-i") == 0 && i + 1 < argc)
            input = argv[i + 1];
        else if (wcscmp(argv[i], L"-progress") == 0)
            prepare = true;
        else if (wcsstr(argv[i], L"f%06d.jpg"))
            frames = argv[i];
        else if (wcsstr(argv[i], L"audio.wav"))
            audio = argv[i];
    }

    if (!prepare && wcsstr(input, L"missing")) {
        write_to(STD_ERROR_HANDLE, "[in#0 @ 0000] Error opening input: No such file or directory\n"
                                   "Error opening input file missing.mp4.\n"
                                   "Error opening input files: No such file or directory\n");
        return 254;
    }
    if (!prepare) {
        write_to(STD_ERROR_HANDLE, "Input #0, mov,mp4,m4a,3gp,3g2,mj2, from 'in.mp4':\n"
                                   "  Duration: 00:00:02.00, start: 0.000000, bitrate: 262 kb/s\n"
                                   "  Stream #0:0[0x1](und): Video: h264 (High), yuv420p, 1920x1080, 30 fps\n");
        if (!wcsstr(input, L"silent"))
            write_to(STD_ERROR_HANDLE, "  Stream #0:1[0x2](und): Audio: aac (LC), 44100 Hz, mono, fltp\n");
        write_to(STD_ERROR_HANDLE, "At least one output file must be specified\n");
        return 1;
    }
    if (wcsstr(input, L"broken")) {
        write_to(STD_ERROR_HANDLE, "[mjpeg @ 0000] Invalid frame\nConversion failed!\n");
        return 1;
    }

    write_to(STD_OUTPUT_HANDLE, "out_time_us=1000000\nprogress=continue\n");
    if (wcsstr(input, L"stuck"))
        Sleep(INFINITE);
    for (int i = 1; i <= 3; i++) {
        wchar_t path[MAX_PATH];
        swprintf(path, MAX_PATH, frames, i);
        CloseHandle(CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL));
    }
    if (audio)
        CloseHandle(CreateFileW(audio, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL));
    write_to(STD_OUTPUT_HANDLE, "out_time_us=2000000\nprogress=end\n");
    LocalFree(argv);
    return 0;
}

static void record_progress(ev_stage stage, int percent, void *context)
{
    convert_fixture *recorded = context;
    if (recorded->progress_count < ARRAYSIZE(recorded->progress))
        recorded->progress[recorded->progress_count++] = (progress_report){ stage, percent };
    if (stage == EV_STAGE_PREPARE && percent > 0)
        SetEvent(recorded->preparing);
}

static void record_log(const wchar_t *line, void *context)
{
    convert_fixture *recorded = context;
    if (recorded->log_count < ARRAYSIZE(recorded->log))
        swprintf(recorded->log[recorded->log_count++], ARRAYSIZE(recorded->log[0]), L"%.500ls", line);
}

static const wchar_t *logged(const wchar_t *start)
{
    for (size_t i = 0; i < fixture.log_count; i++) {
        if (wcsncmp(fixture.log[i], start, wcslen(start)) == 0)
            return fixture.log[i];
    }
    return NULL;
}

static bool reported(ev_stage stage, int percent)
{
    for (size_t i = 0; i < fixture.progress_count; i++) {
        if (fixture.progress[i].stage == stage && fixture.progress[i].percent == percent)
            return true;
    }
    return false;
}

static bool reported_stage(ev_stage stage)
{
    for (size_t i = 0; i < fixture.progress_count; i++) {
        if (fixture.progress[i].stage == stage)
            return true;
    }
    return false;
}

static bool exists(const wchar_t *path)
{
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static bool output_says(const char *expected)
{
    HANDLE file = CreateFileW(fixture.output, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    char text[16] = { 0 };
    DWORD read = 0;
    ReadFile(file, text, sizeof text - 1, &read, NULL);
    CloseHandle(file);
    return strcmp(text, expected) == 0;
}

static const wchar_t *temporary_folder(void)
{
    static const wchar_t lead[] = L"evilvideo: temporary folder is ";
    const wchar_t *line = logged(lead);
    return line ? line + ARRAYSIZE(lead) - 1 : L"";
}

// Sets up a conversion of the given input, with evtests.exe standing in for both ffmpeg and radvideo64.exe.
static void fixture_create(const wchar_t *input)
{
    fixture = (convert_fixture){ .preparing = CreateEventW(NULL, TRUE, FALSE, NULL) };
    ev_job_create(&fixture.job);
    GetModuleFileNameW(NULL, fixture.self, MAX_PATH);
    ev_temp_create(fixture.folder);
    swprintf(fixture.output, MAX_PATH, L"%ls\\out.bik", fixture.folder);
    fixture.conversion = (ev_conversion){
        .input = input,
        .output = fixture.output,
        .game = ev_game_find(L"tssm"),
        .letterbox = true,
        .ffmpeg = fixture.self,
        .rad = fixture.self,
        .on_progress = record_progress,
        .on_log = record_log,
        .context = &fixture,
    };
}

static void convert(void)
{
    fixture.result = ev_convert(&fixture.conversion, &fixture.job, fixture.message, ARRAYSIZE(fixture.message));
}

static DWORD WINAPI convert_thread(void *param)
{
    (void)param;
    convert();
    return 0;
}

static void fixture_delete(void)
{
    ev_temp_delete(fixture.folder);
    ev_job_close(&fixture.job);
    CloseHandle(fixture.preparing);
}

static void convert_writes_output_with_audio(void)
{
    fixture_create(L"C:\\clips\\intro.mp4");

    convert();

    EXPECT(fixture.result == EV_RESULT_CONVERTED && fixture.message[0] == L'\0');
    EXPECT(exists(fixture.output));
    EXPECT(reported_stage(EV_STAGE_MIX));
    EXPECT(reported(EV_STAGE_FINISH, 100));
    fixture_delete();
}

static void convert_skips_mix_without_audio(void)
{
    fixture_create(L"C:\\clips\\silent.mp4");

    convert();

    EXPECT(fixture.result == EV_RESULT_CONVERTED);
    EXPECT(exists(fixture.output));
    EXPECT(!reported_stage(EV_STAGE_MIX));
    fixture_delete();
}

static void convert_maps_stage_progress_to_overall_progress(void)
{
    fixture_create(L"C:\\clips\\intro.mp4");

    convert();

    EXPECT(reported(EV_STAGE_PROBE, 0));
    EXPECT(reported(EV_STAGE_PREPARE, 25));
    EXPECT(reported(EV_STAGE_PREPARE, 50));
    EXPECT(reported(EV_STAGE_COMPRESS, 74));
    EXPECT(reported(EV_STAGE_MIX, 98));
    EXPECT(reported(EV_STAGE_FINISH, 100));
    fixture_delete();
}

static void convert_measures_progress_against_trimmed_length(void)
{
    fixture_create(L"C:\\clips\\intro.mp4");
    fixture.conversion.trim = true;
    fixture.conversion.trim_start = 0;
    fixture.conversion.trim_end = 1;

    convert();

    EXPECT(reported(EV_STAGE_PREPARE, 50));
    EXPECT(!reported(EV_STAGE_PREPARE, 25));
    fixture_delete();
}

static void convert_deletes_temporary_folder(void)
{
    fixture_create(L"C:\\clips\\intro.mp4");

    convert();

    EXPECT(temporary_folder()[0] != L'\0' && !exists(temporary_folder()));
    EXPECT(logged(L"evilvideo: deleted the temporary folder"));
    fixture_delete();
}

static void convert_hides_rad_windows_by_default(void)
{
    fixture_create(L"C:\\clips\\intro.mp4");

    convert();

    EXPECT(output_says("hidden"));
    fixture_delete();
}

static void convert_shows_rad_windows_when_asked(void)
{
    fixture_create(L"C:\\clips\\intro.mp4");
    fixture.conversion.show_rad = true;

    convert();

    EXPECT(output_says("visible"));
    fixture_delete();
}

static void convert_logs_tools_commands_and_findings(void)
{
    fixture_create(L"C:\\clips\\intro.mp4");
    wchar_t tools[MAX_PATH + 32];
    swprintf(tools, ARRAYSIZE(tools), L"evilvideo: ffmpeg is %ls", fixture.self);

    convert();

    EXPECT(logged(tools));
    EXPECT(logged(L"evilvideo: temporary folder is "));
    EXPECT(logged(L"ffmpeg: running "));
    EXPECT(logged(L"ffmpeg:   Duration: 00:00:02.00, start: 0.000000, bitrate: 262 kb/s"));
    EXPECT(logged(L"ffmpeg: exited with code 1 after "));
    EXPECT(logged(L"evilvideo: the input lasts 2.00 s and has audio"));
    EXPECT(logged(L"evilvideo: ffmpeg wrote 3 frames"));
    EXPECT(logged(L"Binkc: finished; closing its window"));
    EXPECT(logged(L"BinkMix: exited with code 0 after "));
    EXPECT(logged(L"evilvideo: saved "));
    fixture_delete();
}

static void convert_reports_unreadable_input(void)
{
    fixture_create(L"C:\\clips\\missing.mp4");

    convert();

    const wchar_t *expected = L"ffmpeg could not read the input: Error opening input files: No such file or directory";
    EXPECT(fixture.result == EV_RESULT_FAILED);
    EXPECT(wcscmp(fixture.message, expected) == 0);
    EXPECT(!exists(fixture.output));
    EXPECT(!exists(temporary_folder()));
    fixture_delete();
}

static void convert_reports_ffmpeg_failure(void)
{
    fixture_create(L"C:\\clips\\broken.mp4");

    convert();

    const wchar_t *expected = L"ffmpeg could not convert the input: [mjpeg @ 0000] Invalid frame; Conversion failed!";
    EXPECT(fixture.result == EV_RESULT_FAILED);
    EXPECT(wcscmp(fixture.message, expected) == 0);
    fixture_delete();
}

static void convert_rejects_trim_past_end(void)
{
    fixture_create(L"C:\\clips\\intro.mp4");
    fixture.conversion.trim = true;
    fixture.conversion.trim_start = 5;
    fixture.conversion.trim_end = 8;

    convert();

    EXPECT(fixture.result == EV_RESULT_FAILED);
    EXPECT(wcscmp(fixture.message, L"The trim starts at 5 s, but the input is only 2 s long.") == 0);
    fixture_delete();
}

static void convert_stops_when_cancelled(void)
{
    fixture_create(L"C:\\clips\\stuck.mp4");
    HANDLE thread = CreateThread(NULL, 0, convert_thread, NULL, 0, NULL);
    bool preparing = WaitForSingleObject(fixture.preparing, 10000) == WAIT_OBJECT_0;

    ev_job_cancel(&fixture.job);

    EXPECT(preparing);
    EXPECT(WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0);
    EXPECT(fixture.result == EV_RESULT_CANCELLED && fixture.message[0] == L'\0');
    EXPECT(!exists(fixture.output));
    EXPECT(!exists(temporary_folder()));
    CloseHandle(thread);
    fixture_delete();
}

static void convert_refuses_cancelled_job(void)
{
    fixture_create(L"C:\\clips\\intro.mp4");
    ev_job_cancel(&fixture.job);

    convert();

    EXPECT(fixture.result == EV_RESULT_CANCELLED);
    EXPECT(!exists(fixture.output));
    fixture_delete();
}

void convert_tests(void)
{
    RUN_TEST(convert_writes_output_with_audio);
    RUN_TEST(convert_skips_mix_without_audio);
    RUN_TEST(convert_maps_stage_progress_to_overall_progress);
    RUN_TEST(convert_measures_progress_against_trimmed_length);
    RUN_TEST(convert_deletes_temporary_folder);
    RUN_TEST(convert_hides_rad_windows_by_default);
    RUN_TEST(convert_shows_rad_windows_when_asked);
    RUN_TEST(convert_logs_tools_commands_and_findings);
    RUN_TEST(convert_reports_unreadable_input);
    RUN_TEST(convert_reports_ffmpeg_failure);
    RUN_TEST(convert_rejects_trim_past_end);
    RUN_TEST(convert_stops_when_cancelled);
    RUN_TEST(convert_refuses_cancelled_job);
}
