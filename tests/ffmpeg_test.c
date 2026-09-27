#include "ffmpeg.h"
#include "test.h"

#include <string.h>
#include <wchar.h>

static const char probe_with_audio[] =
    "Input #0, mov,mp4,m4a,3gp,3g2,mj2, from 'wide.mp4':\n"
    "  Metadata:\n"
    "    major_brand     : isom\n"
    "    encoder         : Lavf62.3.100\n"
    "  Duration: 00:01:02.50, start: 0.000000, bitrate: 262 kb/s\n"
    "  Stream #0:0[0x1](und): Video: h264 (High) (avc1 / 0x31637661), yuv420p(progressive), 1920x1080 [SAR 1:1 DAR "
    "16:9], 177 kb/s, 30 fps, 30 tbr, 15360 tbn (default)\n"
    "    Metadata:\n"
    "      handler_name    : VideoHandler\n"
    "  Stream #0:1[0x2](und): Audio: aac (LC) (mp4a / 0x6134706D), 44100 Hz, mono, fltp, 69 kb/s (default)\n"
    "    Metadata:\n"
    "      handler_name    : SoundHandler\n"
    "At least one output file must be specified\n";

static const char probe_without_audio[] =
    "Input #0, mov,mp4,m4a,3gp,3g2,mj2, from 'tall.mp4':\n"
    "  Metadata:\n"
    "    title           : Audio: none\n"
    "  Duration: 01:00:00.00, start: 0.000000, bitrate: 91 kb/s\n"
    "  Stream #0:0[0x1](und): Video: h264 (High) (avc1 / 0x31637661), yuv420p(progressive), 1080x1920, 30 fps\n"
    "At least one output file must be specified\n";

static const char probe_unknown_duration[] =
    "Input #0, mpegts, from 'live.ts':\n"
    "  Duration: N/A, start: 1.400000, bitrate: N/A\n"
    "  Stream #0:0[0x100]: Video: h264 (Main), yuv420p(progressive), 1280x720, 25 fps\n"
    "At least one output file must be specified\n";

static const char probe_missing_file[] =
    "[in#0 @ 0x5add57736440] Error opening input: No such file or directory\n"
    "Error opening input file nope.mp4.\n"
    "Error opening input files: No such file or directory\n";

static bool frames_folder_writes_frames(const wchar_t *folder, const wchar_t *expected)
{
    ev_prepare prepare = { .input = L"in.mp4", .folder = folder, .game = ev_game_find(L"bfbb") };
    ev_cmdline cmdline = { 0 };

    ev_ffmpeg_prepare_args(&cmdline, L"ffmpeg.exe", &prepare);

    return wcsstr(cmdline.text, expected) != NULL;
}

static void locate_fails_without_ffmpeg_next_to_executable(void)
{
    wchar_t path[MAX_PATH];

    EXPECT(!ev_ffmpeg_locate(path));
}

static void locate_finds_ffmpeg_next_to_executable(void)
{
    wchar_t ffmpeg[MAX_PATH];
    GetModuleFileNameW(NULL, ffmpeg, MAX_PATH);
    wcscpy(wcsrchr(ffmpeg, L'\\') + 1, L"ffmpeg.exe");
    CloseHandle(CreateFileW(ffmpeg, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL));
    wchar_t path[MAX_PATH];

    bool found = ev_ffmpeg_locate(path);

    DeleteFileW(ffmpeg);
    EXPECT(found && wcscmp(path, ffmpeg) == 0);
}

static void probe_args_describe_input(void)
{
    ev_cmdline cmdline = { 0 };

    ev_ffmpeg_probe_args(&cmdline, L"C:\\ev\\ffmpeg.exe", L"C:\\my videos\\in.mp4");

    EXPECT(wcscmp(cmdline.text, L"C:\\ev\\ffmpeg.exe -hide_banner -nostdin -i \"C:\\my videos\\in.mp4\"") == 0);
}

static void prepare_args_stretch_without_trim_or_audio(void)
{
    ev_prepare prepare = {
        .input = L"C:\\in.mp4",
        .folder = L"C:\\t",
        .game = ev_game_find(L"tssm"),
    };
    ev_cmdline cmdline = { 0 };

    ev_ffmpeg_prepare_args(&cmdline, L"C:\\ev\\ffmpeg.exe", &prepare);

    EXPECT(wcscmp(cmdline.text,
                  L"C:\\ev\\ffmpeg.exe -hide_banner -nostdin -y -loglevel error -progress pipe:1 -nostats "
                  L"-i C:\\in.mp4 -map 0:v:0 "
                  L"-vf fps=30000/1001,scale=512:480:flags=lanczos:out_range=full,setsar=1,format=yuv420p "
                  L"-q:v 2 -start_number 1 C:\\t\\frames\\f%06d.jpg") == 0);
}

static void prepare_args_letterbox_with_trim_and_audio(void)
{
    ev_prepare prepare = {
        .input = L"C:\\my videos\\in.mp4",
        .folder = L"C:\\t",
        .game = ev_game_find(L"bfbb"),
        .letterbox = true,
        .trim = true,
        .trim_start = 1.5,
        .trim_end = 12,
        .audio = true,
    };
    ev_cmdline cmdline = { 0 };

    ev_ffmpeg_prepare_args(&cmdline, L"C:\\ev\\ffmpeg.exe", &prepare);

    EXPECT(wcscmp(cmdline.text,
                  L"C:\\ev\\ffmpeg.exe -hide_banner -nostdin -y -loglevel error -progress pipe:1 -nostats "
                  L"-ss 1.500 -t 10.500 -i \"C:\\my videos\\in.mp4\" -map 0:v:0 "
                  L"-vf fps=30000/1001,"
                  L"scale=w='trunc(640*min(1,dar*3/4)/2)*2':h='trunc(480*min(1,4/(3*dar))/2)*2'"
                  L":flags=lanczos:out_range=full,"
                  L"setsar=1,pad=640:480:(ow-iw)/2:(oh-ih)/2:black,format=yuv420p "
                  L"-q:v 2 -start_number 1 C:\\t\\frames\\f%06d.jpg "
                  L"-map 0:a:0 -ac 2 -ar 48000 -c:a pcm_s16le C:\\t\\audio.wav") == 0);
}

static void prepare_args_escape_percent_in_frames_folder(void)
{
    EXPECT(frames_folder_writes_frames(L"C:\\100%\\t", L"C:\\100%%\\t\\frames\\f%06d.jpg"));
}

static void prepare_args_refuse_folder_too_long(void)
{
    wchar_t folder[MAX_PATH + 1];
    wmemset(folder, L'x', MAX_PATH);
    folder[MAX_PATH] = L'\0';
    ev_prepare prepare = { .input = L"in.mp4", .folder = folder, .game = ev_game_find(L"bfbb") };
    ev_cmdline cmdline = { 0 };

    ev_ffmpeg_prepare_args(&cmdline, L"ffmpeg.exe", &prepare);

    EXPECT(cmdline.too_long);
}

static void parse_probe_reads_duration_and_audio(void)
{
    ev_probe probe;

    bool opened = ev_ffmpeg_parse_probe(probe_with_audio, &probe);

    EXPECT(opened);
    EXPECT(probe.duration == 62.5);
    EXPECT(probe.has_audio);
}

static void parse_probe_ignores_audio_outside_stream_lines(void)
{
    ev_probe probe;

    bool opened = ev_ffmpeg_parse_probe(probe_without_audio, &probe);

    EXPECT(opened);
    EXPECT(probe.duration == 3600);
    EXPECT(!probe.has_audio);
}

static void parse_probe_reports_unknown_duration(void)
{
    ev_probe probe;

    bool opened = ev_ffmpeg_parse_probe(probe_unknown_duration, &probe);

    EXPECT(opened);
    EXPECT(probe.duration == -1);
}

static void parse_probe_fails_for_unopenable_input(void)
{
    ev_probe probe;

    EXPECT(!ev_ffmpeg_parse_probe(probe_missing_file, &probe));
}

static void parse_progress_reads_output_time(void)
{
    double seconds = 0;

    bool parsed = ev_ffmpeg_parse_progress("out_time_us=1500000", &seconds);

    EXPECT(parsed && seconds == 1.5);
}

static void parse_progress_ignores_unknown_time(void)
{
    double seconds;

    EXPECT(!ev_ffmpeg_parse_progress("out_time_us=N/A", &seconds));
}

static void parse_progress_ignores_negative_time(void)
{
    double seconds;

    EXPECT(!ev_ffmpeg_parse_progress("out_time_us=-23220", &seconds));
}

static void parse_progress_ignores_other_keys(void)
{
    double seconds;

    EXPECT(!ev_ffmpeg_parse_progress("out_time_ms=1500000", &seconds));
    EXPECT(!ev_ffmpeg_parse_progress("progress=end", &seconds));
}

static void last_lines_keeps_final_lines(void)
{
    EXPECT(strcmp(ev_ffmpeg_last_lines("a\nb\nc\n", 2), "b\nc\n") == 0);
}

static void last_lines_keeps_everything_when_short(void)
{
    EXPECT(strcmp(ev_ffmpeg_last_lines("a\r\nb", 5), "a\r\nb") == 0);
}

static void last_lines_of_missing_file_explain_failure(void)
{
    const char *reason = ev_ffmpeg_last_lines(probe_missing_file, 1);

    EXPECT(strcmp(reason, "Error opening input files: No such file or directory\n") == 0);
}

void ffmpeg_tests(void)
{
    RUN_TEST(locate_fails_without_ffmpeg_next_to_executable);
    RUN_TEST(locate_finds_ffmpeg_next_to_executable);
    RUN_TEST(probe_args_describe_input);
    RUN_TEST(prepare_args_stretch_without_trim_or_audio);
    RUN_TEST(prepare_args_letterbox_with_trim_and_audio);
    RUN_TEST(prepare_args_escape_percent_in_frames_folder);
    RUN_TEST(prepare_args_refuse_folder_too_long);
    RUN_TEST(parse_probe_reads_duration_and_audio);
    RUN_TEST(parse_probe_ignores_audio_outside_stream_lines);
    RUN_TEST(parse_probe_reports_unknown_duration);
    RUN_TEST(parse_probe_fails_for_unopenable_input);
    RUN_TEST(parse_progress_reads_output_time);
    RUN_TEST(parse_progress_ignores_unknown_time);
    RUN_TEST(parse_progress_ignores_negative_time);
    RUN_TEST(parse_progress_ignores_other_keys);
    RUN_TEST(last_lines_keeps_final_lines);
    RUN_TEST(last_lines_keeps_everything_when_short);
    RUN_TEST(last_lines_of_missing_file_explain_failure);
}
