#pragma once

#include "proc.h"

/**
 * Receives Binkc's progress.
 *
 * @param percent How far Binkc has got, from 0 to 100.
 * @param context The context passed to ev_rad_compress.
 */
typedef void (*ev_rad_progress)(int percent, void *context);

/**
 * Finds radvideo64.exe.
 *
 * @param explicit_path A path the user gave for this run (the CLI's --rad), or NULL. When given, it is the only
 *                      candidate.
 * @param saved_path The path saved in settings, or NULL. Used if the file still exists.
 * @param path Receives the full path to radvideo64.exe; holds MAX_PATH characters. Without an explicit or saved path,
 *             it is the standard install under %ProgramFiles(x86)% or %ProgramFiles%.
 * @return false if RAD Video Tools were not found; GetLastError has the reason.
 */
bool ev_rad_locate(const wchar_t *explicit_path, const wchar_t *saved_path, wchar_t path[MAX_PATH]);

/**
 * Builds the Binkc command line, which compresses the frames of a temporary folder into its video.bik.
 *
 * @param cmdline A zero-initialized command line to build.
 * @param rad The full path to radvideo64.exe.
 * @param folder A folder from ev_temp_create.
 * @param frames The number of frames in it, from ev_frames_count.
 */
void ev_rad_binkc_args(ev_cmdline *cmdline, const wchar_t *rad, const wchar_t *folder, int frames);

/**
 * Builds the BinkMix command line, which merges a temporary folder's audio.wav into its video.bik as final.bik.
 *
 * @param cmdline A zero-initialized command line to build.
 * @param rad The full path to radvideo64.exe.
 * @param folder A folder from ev_temp_create.
 */
void ev_rad_binkmix_args(ev_cmdline *cmdline, const wchar_t *rad, const wchar_t *folder);

/**
 * Reads a RAD tool's state from its window title, such as "43% - Bink Video Compressor" or
 * "Bink Audio Mixer - Done!".
 *
 * @param title The title of a window of class RADClass.
 * @param percent Receives the percentage the title starts with, or -1 when it has none.
 * @return Whether the title reports that the tool finished and its output is complete.
 */
bool ev_rad_parse_title(const wchar_t *title, int *percent);

/**
 * Runs Binkc on a temporary folder's frames, hidden, and waits until video.bik is complete.
 *
 * The tool never exits on its own, so this watches its windows: it reports the title's percentage, closes the window
 * once the title reports it is done, and closes any error dialog, keeping its text as the failure message.
 *
 * @param job The job to run Binkc in. Terminating the job ends the run early, as a failure.
 * @param rad The full path to radvideo64.exe.
 * @param folder A folder from ev_temp_create.
 * @param frames The number of frames in it, from ev_frames_count.
 * @param on_progress Called with each new percentage; may be NULL.
 * @param context Passed to on_progress.
 * @param message Receives why the run failed, or an empty string on success.
 * @param message_size The size of message in characters.
 * @return Whether video.bik was written.
 */
bool ev_rad_compress(HANDLE job, const wchar_t *rad, const wchar_t *folder, int frames, ev_rad_progress on_progress,
                     void *context, wchar_t *message, size_t message_size);

/**
 * Runs BinkMix on a temporary folder, hidden, and waits until final.bik is complete. It is watched as ev_rad_compress
 * describes, but reports no progress: it finishes in about a second.
 *
 * @param job The job to run BinkMix in. Terminating the job ends the run early, as a failure.
 * @param rad The full path to radvideo64.exe.
 * @param folder A folder from ev_temp_create, holding video.bik and audio.wav.
 * @param message Receives why the run failed, or an empty string on success.
 * @param message_size The size of message in characters.
 * @return Whether final.bik was written.
 */
bool ev_rad_mix(HANDLE job, const wchar_t *rad, const wchar_t *folder, wchar_t *message, size_t message_size);
