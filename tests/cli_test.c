#include "args.h"
#include "files.h"
#include "test.h"

#include <stdlib.h>
#include <wchar.h>
#include <windows.h>

#define ARGS(...) ((wchar_t *[]){ L"evilvideo-cli", __VA_ARGS__, NULL })

static wchar_t message[256];

static bool parse(cli_options *options, wchar_t **args)
{
    int argc = 0;
    while (args[argc])
        argc++;
    return cli_parse(argc, args, options, message, ARRAYSIZE(message));
}

static bool rejects(wchar_t **args)
{
    cli_options options;
    message[0] = L'\0';
    return !parse(&options, args) && message[0] != L'\0';
}

static void create_folder(wchar_t folder[MAX_PATH])
{
    GetTempPathW(MAX_PATH, folder);
    swprintf(folder + wcslen(folder), MAX_PATH - wcslen(folder), L"evilvideo cli test %lu",
             GetCurrentProcessId());
    CreateDirectoryW(folder, NULL);
}

static void create_file(const wchar_t *folder, const wchar_t *name)
{
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%ls\\%ls", folder, name);
    CloseHandle(CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL));
}

static bool output_path_is(const wchar_t *input, const wchar_t *output, bool folder, const wchar_t *expected)
{
    wchar_t *path = cli_output_path(input, output, folder);
    bool matches = wcscmp(path, expected) == 0;
    free(path);
    return matches;
}

static void parse_reads_every_option(void)
{
    cli_options options;

    bool parsed = parse(&options, ARGS(L"-g", L"TSSM", L"-o", L"out", L"--stretch", L"--trim", L"1.5-12", L"-y",
                                       L"--rad", L"rad.exe", L"--show-rad", L"-v", L"in.mp4"));

    EXPECT(parsed);
    EXPECT(options.game == ev_game_find(L"tssm"));
    EXPECT(wcscmp(options.output, L"out") == 0);
    EXPECT(!options.letterbox);
    EXPECT(options.trim && options.trim_start == 1.5 && options.trim_end == 12);
    EXPECT(options.overwrite);
    EXPECT(wcscmp(options.rad, L"rad.exe") == 0);
    EXPECT(options.show_rad && options.verbose && !options.quiet);
}

static void parse_reads_long_forms(void)
{
    cli_options options;

    bool parsed = parse(&options, ARGS(L"--game", L"bfbb", L"--output", L"out", L"--overwrite", L"--quiet", L"in"));

    EXPECT(parsed && options.game == ev_game_find(L"bfbb") && wcscmp(options.output, L"out") == 0);
    EXPECT(options.overwrite && options.quiet);
}

static void parse_defaults_to_letterbox_and_nothing_else(void)
{
    cli_options options;

    bool parsed = parse(&options, ARGS(L"-g", L"rotu", L"in.mp4"));

    EXPECT(parsed && options.letterbox);
    EXPECT(!options.output && !options.trim && !options.overwrite && !options.rad && !options.show_rad);
    EXPECT(!options.verbose && !options.quiet && !options.help && !options.version);
}

static void parse_collects_inputs_between_options(void)
{
    cli_options options;

    bool parsed = parse(&options, ARGS(L"a.mp4", L"-g", L"n100f", L"b.mkv", L"-y", L"c*.avi"));

    EXPECT(parsed && options.input_count == 3);
    EXPECT(wcscmp(options.inputs[0], L"a.mp4") == 0);
    EXPECT(wcscmp(options.inputs[1], L"b.mkv") == 0);
    EXPECT(wcscmp(options.inputs[2], L"c*.avi") == 0);
}

static void parse_stops_at_help_or_version(void)
{
    cli_options help, version;

    EXPECT(parse(&help, ARGS(L"--help", L"--bogus")) && help.help);
    EXPECT(parse(&version, ARGS(L"--version")) && version.version);
}

static void parse_rejects_usage_errors(void)
{
    EXPECT(rejects(ARGS(L"-g", L"bfbb", L"--bogus", L"in")));
    EXPECT(rejects(ARGS(L"in", L"-g")));
    EXPECT(rejects(ARGS(L"-g", L"bfbb", L"-o", L"-v", L"out.bik", L"in")));
    EXPECT(rejects(ARGS(L"in")));
    EXPECT(rejects(ARGS(L"-g", L"rat", L"in")));
    EXPECT(rejects(ARGS(L"-g", L"bfbb")));
    EXPECT(rejects(ARGS(L"-g", L"bfbb", L"-v", L"-q", L"in")));
}

static void parse_accepts_trim_ranges(void)
{
    cli_options options;

    EXPECT(parse(&options, ARGS(L"-g", L"bfbb", L"--trim", L"0-3", L"in")) && options.trim_start == 0 &&
           options.trim_end == 3);
    EXPECT(parse(&options, ARGS(L"-g", L"bfbb", L"--trim", L"2.25-2.5", L"in")) && options.trim_start == 2.25 &&
           options.trim_end == 2.5);
}

static void parse_rejects_malformed_trims(void)
{
    static const wchar_t *const trims[] = { L"5-2", L"3-3", L"-1-3", L"1-", L"-3", L"a-b", L"1-2x", L"1", L"1-inf" };

    for (size_t i = 0; i < ARRAYSIZE(trims); i++) {
        wchar_t trim[16];
        wcscpy(trim, trims[i]);

        EXPECT(rejects(ARGS(L"-g", L"bfbb", L"--trim", trim, L"in")));
    }
}

static void file_name_follows_last_separator(void)
{
    EXPECT(wcscmp(cli_file_name(L"C:\\clips/intro.mp4"), L"intro.mp4") == 0);
    EXPECT(wcscmp(cli_file_name(L"C:intro.mp4"), L"intro.mp4") == 0);
    EXPECT(wcscmp(cli_file_name(L"intro.mp4"), L"intro.mp4") == 0);
}

static void is_pattern_only_looks_at_file_name(void)
{
    EXPECT(cli_is_pattern(L"clips\\*.mp4"));
    EXPECT(cli_is_pattern(L"clip?.mp4"));
    EXPECT(!cli_is_pattern(L"\\\\?\\C:\\clips\\intro.mp4"));
}

static void expand_lists_matching_files_sorted(void)
{
    wchar_t folder[MAX_PATH], pattern[MAX_PATH], expected[MAX_PATH];
    create_folder(folder);
    create_file(folder, L"b.mp4");
    create_file(folder, L"A.mp4");
    create_file(folder, L"c.txt");
    swprintf(pattern, MAX_PATH, L"%ls\\sub.mp4", folder);
    CreateDirectoryW(pattern, NULL);
    swprintf(pattern, MAX_PATH, L"%ls\\*.mp4", folder);
    int count;

    wchar_t **paths = cli_expand(pattern, &count);

    EXPECT(count == 2);
    swprintf(expected, MAX_PATH, L"%ls\\A.mp4", folder);
    EXPECT(count == 2 && wcscmp(paths[0], expected) == 0);
    swprintf(expected, MAX_PATH, L"%ls\\b.mp4", folder);
    EXPECT(count == 2 && wcscmp(paths[1], expected) == 0);
    for (int i = 0; i < count; i++)
        free(paths[i]);
    free(paths);
    ev_temp_delete(folder);
}

static void expand_returns_nothing_without_a_match(void)
{
    wchar_t folder[MAX_PATH], pattern[MAX_PATH];
    create_folder(folder);
    swprintf(pattern, MAX_PATH, L"%ls\\*.mp4", folder);
    int count;

    wchar_t **paths = cli_expand(pattern, &count);

    EXPECT(paths == NULL && count == 0);
    ev_temp_delete(folder);
}

static void output_is_folder_for_several_inputs_separator_or_folder(void)
{
    wchar_t folder[MAX_PATH];
    create_folder(folder);

    EXPECT(cli_output_is_folder(L"missing", true));
    EXPECT(cli_output_is_folder(L"missing\\", false));
    EXPECT(cli_output_is_folder(L"missing/", false));
    EXPECT(cli_output_is_folder(folder, false));
    EXPECT(!cli_output_is_folder(L"missing.bik", false));
    ev_temp_delete(folder);
}

static void output_path_defaults_to_next_to_input(void)
{
    EXPECT(output_path_is(L"C:\\clips\\intro.mp4", NULL, false, L"C:\\clips\\intro.bik"));
    EXPECT(output_path_is(L"clips/intro.final.mkv", NULL, false, L"clips/intro.final.bik"));
    EXPECT(output_path_is(L"intro", NULL, false, L"intro.bik"));
}

static void output_path_puts_input_name_in_folder(void)
{
    EXPECT(output_path_is(L"C:\\clips\\intro.mp4", L"out", true, L"out\\intro.bik"));
    EXPECT(output_path_is(L"C:\\clips\\intro.mp4", L"out\\", true, L"out\\intro.bik"));
    EXPECT(output_path_is(L"intro.mp4", L"out/", true, L"out/intro.bik"));
}

static void output_path_uses_file_as_is(void)
{
    EXPECT(output_path_is(L"C:\\clips\\intro.mp4", L"D:\\game\\fmv\\boot.bik", false, L"D:\\game\\fmv\\boot.bik"));
}

void cli_tests(void)
{
    RUN_TEST(parse_reads_every_option);
    RUN_TEST(parse_reads_long_forms);
    RUN_TEST(parse_defaults_to_letterbox_and_nothing_else);
    RUN_TEST(parse_collects_inputs_between_options);
    RUN_TEST(parse_stops_at_help_or_version);
    RUN_TEST(parse_rejects_usage_errors);
    RUN_TEST(parse_accepts_trim_ranges);
    RUN_TEST(parse_rejects_malformed_trims);
    RUN_TEST(file_name_follows_last_separator);
    RUN_TEST(is_pattern_only_looks_at_file_name);
    RUN_TEST(expand_lists_matching_files_sorted);
    RUN_TEST(expand_returns_nothing_without_a_match);
    RUN_TEST(output_is_folder_for_several_inputs_separator_or_folder);
    RUN_TEST(output_path_defaults_to_next_to_input);
    RUN_TEST(output_path_puts_input_name_in_folder);
    RUN_TEST(output_path_uses_file_as_is);
}
