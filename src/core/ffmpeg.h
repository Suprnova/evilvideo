#pragma once

#include "games.h"
#include "proc.h"

/** What the probe learned about an input. */
typedef struct ev_probe {
    /** Length of the input in seconds, or -1 when ffmpeg does not know it. */
    double duration;
    /** Whether the input has an audio stream. */
    bool has_audio;
} ev_probe;

/** What the prepare stage writes, and from what. */
typedef struct ev_prepare {
    /** The input video, in any form ffmpeg accepts. */
    const wchar_t *input;
    /**
     * The temporary folder, shorter than MAX_PATH. Its frames subfolder must already exist; the frames are written
     * there as f000001.jpg onward, and the audio as audio.wav.
     */
    const wchar_t *folder;
    /** The game whose output size to scale to. */
    const ev_game *game;
    /** Whether to letterbox the picture to the 4:3 display rather than stretch it over the frame. */
    bool letterbox;
    /** Whether to keep only the range from trim_start to trim_end. */
    bool trim;
    /** Start of the kept range in seconds, when trimming. */
    double trim_start;
    /** End of the kept range in seconds, when trimming. Must be greater than trim_start. */
    double trim_end;
    /** Whether to write audio.wav. Only valid when the probe found audio. */
    bool audio;
} ev_prepare;

/**
 * Finds ffmpeg.exe in the running executable's folder.
 *
 * @param path Receives the full path to ffmpeg.exe; holds MAX_PATH characters.
 * @return false if ffmpeg.exe is not there; GetLastError has the reason.
 */
bool ev_ffmpeg_locate(wchar_t path[MAX_PATH]);

/**
 * Builds the probe command line, which makes ffmpeg describe the input on standard error.
 *
 * ffmpeg exits with an error because the command line has no output; that is expected, and only its standard error
 * matters. Pass that to ev_ffmpeg_parse_probe.
 *
 * @param cmdline A zero-initialized command line to build.
 * @param ffmpeg The full path to ffmpeg.exe.
 * @param input The input video.
 */
void ev_ffmpeg_probe_args(ev_cmdline *cmdline, const wchar_t *ffmpeg, const wchar_t *input);

/**
 * Builds the prepare command line, which converts the input into the game's JPEG frames at 29.97 fps and, when asked,
 * a 48 kHz stereo 16-bit WAV.
 *
 * ffmpeg reports progress on standard output as -progress lines (pass them to ev_ffmpeg_parse_progress) and prints only
 * errors on standard error.
 *
 * @param cmdline A zero-initialized command line to build. Marked too long if prepare->folder is not shorter than
 *                MAX_PATH.
 * @param ffmpeg The full path to ffmpeg.exe.
 * @param prepare What to write, and from what.
 */
void ev_ffmpeg_prepare_args(ev_cmdline *cmdline, const wchar_t *ffmpeg, const ev_prepare *prepare);

/**
 * Reads the input's description from the probe's standard error.
 *
 * @param errors ffmpeg's standard error, null-terminated.
 * @param probe Receives the duration and whether there is audio.
 * @return false if ffmpeg could not open the input; ev_ffmpeg_last_lines then gives its reason.
 */
bool ev_ffmpeg_parse_probe(const char *errors, ev_probe *probe);

/**
 * Reads how far the prepare stage has got from one line of its standard output.
 *
 * @param line One line of -progress output, without its line break.
 * @param seconds Receives how many seconds of output ffmpeg has written.
 * @return false if the line does not give a time: it holds another key, or the time is not known yet.
 */
bool ev_ffmpeg_parse_progress(const char *line, double *seconds);

/**
 * Finds the start of the last lines of ffmpeg's standard error, which explain why it failed.
 *
 * @param errors ffmpeg's standard error, null-terminated.
 * @param count How many lines to keep. Trailing line breaks do not count as lines.
 * @return A pointer into errors where the last count lines start, or errors itself when it has no more lines than
 *         that. The text keeps its trailing line breaks.
 */
const char *ev_ffmpeg_last_lines(const char *errors, int count);
