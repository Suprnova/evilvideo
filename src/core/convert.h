#pragma once

#include "games.h"
#include "rad.h"

/** A stage of a conversion, in order. */
typedef enum ev_stage {
    /** ffmpeg reads the input's length and whether it has audio. */
    EV_STAGE_PROBE,
    /** ffmpeg writes the frames and audio. */
    EV_STAGE_PREPARE,
    /** Binkc compresses the frames. */
    EV_STAGE_COMPRESS,
    /** BinkMix adds the audio; skipped when the input has none. */
    EV_STAGE_MIX,
    /** The result moves to the output path. */
    EV_STAGE_FINISH,
} ev_stage;

/** How a conversion ended. */
typedef enum ev_result {
    /** The output was written. */
    EV_RESULT_CONVERTED,
    /** The job was cancelled; the output was not touched. */
    EV_RESULT_CANCELLED,
    /** The conversion failed; the message says why, and the output was not touched. */
    EV_RESULT_FAILED,
} ev_result;

/**
 * Receives a conversion's progress.
 *
 * @param stage The current stage.
 * @param percent Overall progress from 0 to 100, or -1 while unknown. Prepare covers 0 to 50, compress 50 to 98, and
 *                mix and finish the rest.
 * @param context The conversion's context.
 */
typedef void (*ev_progress_callback)(ev_stage stage, int percent, void *context);

/** One video to convert, and how. */
typedef struct ev_conversion {
    /** The input video, in any form ffmpeg reads. */
    const wchar_t *input;
    /** The .bik to write. Its folder must exist; an existing file is replaced. */
    const wchar_t *output;
    /** The game to convert for. */
    const ev_game *game;
    /** Whether to letterbox the picture to the 4:3 display rather than stretch it over the frame. */
    bool letterbox;
    /** Whether to keep only the range from trim_start to trim_end. */
    bool trim;
    /** Start of the kept range in seconds, when trimming. At least 0. */
    double trim_start;
    /** End of the kept range in seconds, when trimming. Greater than trim_start. */
    double trim_end;
    /** The full path to ffmpeg.exe, from ev_ffmpeg_locate. */
    const wchar_t *ffmpeg;
    /** The full path to radvideo64.exe, from ev_rad_locate. */
    const wchar_t *rad;
    /** Whether to show the RAD tools' windows, without taking focus, instead of hiding them. */
    bool show_rad;
    /** Called when the stage or percentage changes; may be NULL. */
    ev_progress_callback on_progress;
    /**
     * Called with each line of the detailed log; may be NULL. The log has the tools and temporary folder in use, every
     * command line run, ffmpeg's messages, what the probe found, the frame count, what the RAD window watcher saw
     * (see ev_rad_run), each tool's exit code and run time, and the cleanup.
     */
    ev_log_callback on_log;
    /** Passed to on_progress and on_log. */
    void *context;
} ev_conversion;

/**
 * Converts one video, synchronously on the calling thread.
 *
 * The work happens in a new temporary folder, which is deleted whatever the outcome; the output path is only written
 * once the result is complete.
 *
 * @param conversion What to convert, and how.
 * @param job The job to run the tools in, from ev_job_create. Cancelling it from another thread ends the conversion
 *            with EV_RESULT_CANCELLED. A cancelled job refuses every later conversion, so a front end that keeps going
 *            after a cancel needs a new job.
 * @param message Receives why the conversion failed, or an empty string.
 * @param message_size The size of message in characters.
 * @return How the conversion ended.
 */
ev_result ev_convert(const ev_conversion *conversion, ev_job *job, wchar_t *message, size_t message_size);
