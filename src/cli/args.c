#include "args.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <windows.h>

static bool usage(wchar_t *message, size_t size, const wchar_t *format, const wchar_t *arg)
{
    swprintf(message, size, format, arg);
    return false;
}

static bool is(const wchar_t *arg, const wchar_t *short_name, const wchar_t *long_name)
{
    return (short_name && wcscmp(arg, short_name) == 0) || wcscmp(arg, long_name) == 0;
}

static bool parse_trim(const wchar_t *text, cli_options *options)
{
    wchar_t *end;
    double start = wcstod(text, &end);
    if (end == text || *end != L'-')
        return false;

    const wchar_t *second = end + 1;
    double stop = wcstod(second, &end);
    if (end == second || *end != L'\0' || !(start >= 0 && start < stop) || !isfinite(stop))
        return false;

    options->trim = true;
    options->trim_start = start;
    options->trim_end = stop;
    return true;
}

bool cli_parse(int argc, wchar_t **argv, cli_options *options, wchar_t *message, size_t message_size)
{
    *options = (cli_options){ .letterbox = true, .inputs = argv + 1 };
    message[0] = L'\0';

    for (int i = 1; i < argc; i++) {
        const wchar_t *arg = argv[i];
        if (arg[0] != L'-') {
            options->inputs[options->input_count++] = argv[i];
            continue;
        }

        if (is(arg, L"-h", L"--help")) {
            options->help = true;
            return true;
        }
        if (is(arg, NULL, L"--version")) {
            options->version = true;
            return true;
        }

        if (is(arg, NULL, L"--stretch"))
            options->letterbox = false;
        else if (is(arg, L"-y", L"--overwrite"))
            options->overwrite = true;
        else if (is(arg, NULL, L"--show-rad"))
            options->show_rad = true;
        else if (is(arg, L"-v", L"--verbose"))
            options->verbose = true;
        else if (is(arg, L"-q", L"--quiet"))
            options->quiet = true;
        else if (is(arg, L"-g", L"--game") || is(arg, L"-o", L"--output") || is(arg, NULL, L"--trim") ||
                 is(arg, NULL, L"--rad")) {
            if (i + 1 == argc || argv[i + 1][0] == L'-')
                return usage(message, message_size, L"%ls needs a value", arg);
            const wchar_t *value = argv[++i];
            if (is(arg, L"-o", L"--output"))
                options->output = value;
            else if (is(arg, NULL, L"--rad"))
                options->rad = value;
            else if (is(arg, NULL, L"--trim") && !parse_trim(value, options))
                return usage(message, message_size,
                             L"--trim needs <from>-<to> in seconds with from before to, such as 1.5-12, not %ls",
                             value);
            else if (is(arg, L"-g", L"--game") && !(options->game = ev_game_find(value)))
                return usage(message, message_size, L"unknown game %ls", value);
        } else {
            return usage(message, message_size, L"unknown option %ls", arg);
        }
    }

    if (options->verbose && options->quiet)
        return usage(message, message_size, L"%ls", L"--verbose and --quiet cannot be combined");
    if (!options->game)
        return usage(message, message_size, L"%ls", L"--game is required");
    if (options->input_count == 0)
        return usage(message, message_size, L"%ls", L"no input given");
    return true;
}

static bool is_separator(wchar_t c)
{
    return c == L'\\' || c == L'/';
}

const wchar_t *cli_file_name(const wchar_t *path)
{
    const wchar_t *name = path;
    for (const wchar_t *c = path; *c; c++) {
        if (is_separator(*c) || *c == L':')
            name = c + 1;
    }
    return name;
}

bool cli_is_pattern(const wchar_t *input)
{
    return wcspbrk(cli_file_name(input), L"*?") != NULL;
}

static int compare_paths(const void *a, const void *b)
{
    return _wcsicmp(*(wchar_t *const *)a, *(wchar_t *const *)b);
}

wchar_t **cli_expand(const wchar_t *pattern, int *count)
{
    *count = 0;
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileExW(pattern, FindExInfoBasic, &data, FindExSearchNameMatch, NULL, 0);
    if (find == INVALID_HANDLE_VALUE)
        return NULL;

    int folder = (int)(cli_file_name(pattern) - pattern);
    wchar_t **paths = NULL;
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        size_t size = folder + wcslen(data.cFileName) + 1;
        paths = cli_checked(realloc(paths, (*count + 1) * sizeof *paths));
        paths[*count] = cli_checked(malloc(size * sizeof **paths));
        swprintf(paths[(*count)++], size, L"%.*ls%ls", folder, pattern, data.cFileName);
    } while (FindNextFileW(find, &data));
    FindClose(find);

    if (paths)
        qsort(paths, *count, sizeof *paths, compare_paths);
    return paths;
}

bool cli_output_is_folder(const wchar_t *output, bool several)
{
    size_t length = wcslen(output);
    DWORD attributes = GetFileAttributesW(output);
    return several || (length > 0 && is_separator(output[length - 1])) ||
           (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY));
}

wchar_t *cli_output_path(const wchar_t *input, const wchar_t *output, bool folder)
{
    if (output && !folder)
        return cli_checked(_wcsdup(output));

    const wchar_t *name = cli_file_name(input);
    const wchar_t *dot = wcsrchr(name, L'.');
    int stem = (int)(dot ? dot - name : (ptrdiff_t)wcslen(name));
    const wchar_t *base = output ? output : input;
    int base_length = (int)(output ? wcslen(output) : (size_t)(name - input));
    const wchar_t *separator = output && base_length > 0 && !is_separator(output[base_length - 1]) ? L"\\" : L"";

    size_t size = base_length + wcslen(separator) + stem + 5;
    wchar_t *path = cli_checked(malloc(size * sizeof *path));
    swprintf(path, size, L"%.*ls%ls%.*ls.bik", base_length, base, separator, stem, name);
    return path;
}

void *cli_checked(void *memory)
{
    if (!memory) {
        fputs("evilvideo-cli: out of memory\n", stderr);
        exit(1);
    }
    return memory;
}
