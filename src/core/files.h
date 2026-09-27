#pragma once

#include <stdbool.h>
#include <windows.h>

/**
 * Creates a new temporary folder for one conversion, with an empty frames subfolder.
 *
 * The folder is %TEMP%\evilvideo-<pid>-<n>, named in its 8.3 short form if the full path is not plain ASCII, since the
 * RAD tools cannot open paths outside the ANSI code page. If even that is not ASCII, the folder goes in
 * %ProgramData%\evilvideo instead.
 *
 * @param folder Receives the folder's path, without a trailing backslash; holds MAX_PATH characters.
 * @return false if no folder could be created; GetLastError has the reason.
 */
bool ev_temp_create(wchar_t folder[MAX_PATH]);

/**
 * Deletes a folder and everything in it. Links inside it are removed without touching what they point to.
 *
 * @param folder The folder to delete.
 * @return false if anything was left behind, for example a file still open in another process.
 */
bool ev_temp_delete(const wchar_t *folder);

/**
 * Deletes the temporary folders of evilvideo processes that are no longer running, left behind by a crash or a forced
 * close. Folders of running processes, including this one, are left alone.
 */
void ev_temp_cleanup(void);

/**
 * Counts the frames the prepare stage wrote.
 *
 * @param folder A folder from ev_temp_create.
 * @return The number of f*.jpg files in its frames subfolder, or -1 if it could not be read.
 */
int ev_frames_count(const wchar_t *folder);
