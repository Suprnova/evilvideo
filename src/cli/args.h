#pragma once

#include "games.h"

#include <stdbool.h>
#include <stddef.h>

/** What the command line asks for. */
typedef struct cli_options {
    /** The game to convert for. */
    const ev_game *game;
    /** The --output path, or NULL to write each output next to its input. */
    const wchar_t *output;
    /** Whether to letterbox rather than stretch (--stretch clears it). */
    bool letterbox;
    /** Whether --trim was given. */
    bool trim;
    /** Start of the --trim range in seconds. */
    double trim_start;
    /** End of the --trim range in seconds. */
    double trim_end;
    /** Whether to replace existing outputs instead of skipping them. */
    bool overwrite;
    /** The --rad path, or NULL to use the saved path or the standard install. */
    const wchar_t *rad;
    /** Whether to show the RAD tools' windows. */
    bool show_rad;
    /** Whether to print the detailed log. */
    bool verbose;
    /** Whether to print only errors and the summary. */
    bool quiet;
    /** Whether to show the help and stop; nothing else is filled in. */
    bool help;
    /** Whether to show the version and stop; nothing else is filled in. */
    bool version;
    /** The input arguments, which may be wildcard patterns. Points into argv. */
    wchar_t **inputs;
    /** Number of input arguments. */
    int input_count;
} cli_options;

/**
 * Parses the command line.
 *
 * Options and inputs may come in any order. The inputs are moved to the start of argv, after the program name, and
 * options->inputs points at them there.
 *
 * @param argc Number of arguments, including the program name.
 * @param argv The arguments, as passed to wmain.
 * @param options Receives the options.
 * @param message Receives what is wrong with the command line.
 * @param message_size The size of message in characters.
 * @return false on a usage error: an unknown option, a missing value (including one that starts with -, which is
 *         taken for a forgotten value followed by the next option), a missing or unknown game, a malformed --trim,
 *         --verbose with --quiet, or no input.
 */
bool cli_parse(int argc, wchar_t **argv, cli_options *options, wchar_t *message, size_t message_size);

/**
 * Finds the file name in a path.
 *
 * @param path A path.
 * @return A pointer into path after its last separator or drive colon.
 */
const wchar_t *cli_file_name(const wchar_t *path);

/**
 * Tells whether an input is a wildcard pattern: its file name contains * or ?.
 *
 * @param input An input argument.
 * @return Whether the input needs cli_expand.
 */
bool cli_is_pattern(const wchar_t *input);

/**
 * Lists the files a wildcard pattern matches, sorted by name. Folders are left out.
 *
 * @param pattern A path whose file name contains * or ?.
 * @param count Receives the number of files.
 * @return The files' paths, in the pattern's folder, or NULL when nothing matches. Free each path and the array with
 *         free.
 */
wchar_t **cli_expand(const wchar_t *pattern, int *count);

/**
 * Tells whether --output names a folder rather than a file.
 *
 * @param output The --output path.
 * @param several Whether the inputs can name more than one file: several input arguments, or a pattern.
 * @return true with several inputs, when output ends in a separator, or when it is an existing folder.
 */
bool cli_output_is_folder(const wchar_t *output, bool several);

/**
 * Builds an input's output path.
 *
 * @param input The input file.
 * @param output The --output path, or NULL to write next to the input.
 * @param folder Whether output is a folder, from cli_output_is_folder.
 * @return output itself when it is a file; otherwise the input's name with a .bik extension, in output or in the
 *         input's folder. Free it with free.
 */
wchar_t *cli_output_path(const wchar_t *input, const wchar_t *output, bool folder);

/**
 * Checks an allocation, ending the program with an error if it failed.
 *
 * @param memory What malloc, realloc or _wcsdup returned.
 * @return memory, which is never NULL.
 */
void *cli_checked(void *memory);
