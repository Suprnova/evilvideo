#pragma once

#include <stdbool.h>
#include <windows.h>

/** Maximum length of a command line in characters, including its terminating null: the CreateProcessW limit. */
#define EV_CMDLINE_MAX 32767

/**
 * A command line being built for a child process.
 *
 * Zero-initialize it, then add the program's full path and each argument in turn with ev_cmdline_add.
 */
typedef struct ev_cmdline {
    /** The command line so far, null-terminated. */
    wchar_t text[EV_CMDLINE_MAX];
    /** Length of text in characters, excluding its terminating null. */
    size_t length;
    /** Whether an argument did not fit. It and every later argument are dropped, and no process starts from the command line. */
    bool too_long;
} ev_cmdline;

/**
 * Receives one line of a child's standard output.
 *
 * @param line The line, null-terminated, without its line break.
 * @param context The context passed to ev_process_run.
 */
typedef void (*ev_line_callback)(const char *line, void *context);

/**
 * Appends an argument to a command line.
 *
 * An argument with no spaces, tabs, line breaks or quotes is added as is. Any other argument is wrapped in quotes,
 * its quotes are escaped with backslashes, and backslashes that precede a quote are doubled.
 *
 * @param cmdline The command line to append to.
 * @param arg The argument.
 */
void ev_cmdline_add(ev_cmdline *cmdline, const wchar_t *arg);

/**
 * Describes a Win32 error for the user, as "<what>: <the system's message>".
 *
 * @param message Receives the description.
 * @param size The size of message in characters.
 * @param what What failed, such as L"Could not start ffmpeg".
 * @param error The error code, usually from GetLastError.
 */
void ev_error_message(wchar_t *message, size_t size, const wchar_t *what, DWORD error);

/**
 * The job object the processes of one conversion run in. Closing it ends them, even when evilvideo crashes; cancelling
 * it ends them and refuses any started afterwards.
 */
typedef struct ev_job {
    /** The job object. */
    HANDLE handle;
    /** Held while a process is being started or the job cancelled, so that a cancel never misses a new process. */
    SRWLOCK lock;
    /** Whether the job was cancelled. */
    bool cancelled;
} ev_job;

/**
 * Creates a job whose processes are terminated when it is closed.
 *
 * @param job Receives the job. Close it with ev_job_close.
 * @return false on failure; GetLastError has the reason.
 */
bool ev_job_create(ev_job *job);

/**
 * Cancels a job: terminates its processes with exit code ERROR_CANCELLED, and makes every later start in it fail with
 * ERROR_CANCELLED. Safe to call from any thread, such as a Cancel button's or a Ctrl+C handler's.
 *
 * @param job The job to cancel.
 */
void ev_job_cancel(ev_job *job);

/**
 * Tells whether a job was cancelled. Safe to call from any thread.
 *
 * @param job The job.
 * @return Whether ev_job_cancel was called on it.
 */
bool ev_job_cancelled(ev_job *job);

/**
 * Closes a job, terminating any processes still in it.
 *
 * @param job The job to close.
 */
void ev_job_close(ev_job *job);

/**
 * Starts a program inside a job without waiting for it.
 *
 * The program starts suspended, is assigned to the job, and only then runs, so it cannot escape the job. It gets no
 * console, and its window starts hidden unless asked otherwise.
 *
 * @param job The job to run the program in.
 * @param cmdline The command line, starting with the program's full path. CreateProcessW may modify its text.
 * @param visible Whether to show the program's window, without taking focus from the current window.
 * @param process Receives the process handle. Close it with CloseHandle.
 * @return false if the program could not be started; GetLastError has the reason, which is
 *         ERROR_FILENAME_EXCED_RANGE when the command line is too long and ERROR_CANCELLED when the job was
 *         cancelled.
 */
bool ev_process_start(ev_job *job, ev_cmdline *cmdline, bool visible, HANDLE *process);

/**
 * Runs a console program inside a job, as ev_process_start does, and waits for it to exit.
 *
 * Standard output is passed on line by line while the program runs. Standard error is collected in full on a separate
 * thread, so a program that writes a lot of it never blocks.
 *
 * @param job The job to run the program in. Cancelling the job ends the run early.
 * @param cmdline The command line, starting with the program's full path. CreateProcessW may modify its text.
 * @param on_line Called with each line of standard output; NULL ignores standard output.
 * @param context Passed to on_line.
 * @param errors Receives everything the program wrote to standard error, null-terminated, or NULL on failure.
 *               Free it with free.
 * @param exit_code Receives the program's exit code.
 * @return false if the program could not be started or its output could not be read; GetLastError has the reason.
 */
bool ev_process_run(ev_job *job, ev_cmdline *cmdline, ev_line_callback on_line, void *context, char **errors,
                    DWORD *exit_code);
