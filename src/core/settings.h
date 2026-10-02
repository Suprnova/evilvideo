#pragma once

#include "games.h"

#include <stdbool.h>
#include <windows.h>

/** What evilvideo remembers between runs, in %APPDATA%\evilvideo\evilvideo.ini. Only the GUI saves it. */
typedef struct ev_settings {
    /** radvideo64.exe as the user located it, or an empty string. */
    wchar_t rad[MAX_PATH];
    /** The game last converted for, or NULL. */
    const ev_game *game;
    /** Whether the last conversion letterboxed. */
    bool letterbox;
} ev_settings;

/**
 * Reads the saved settings.
 *
 * @param settings Receives the settings. Anything not saved, or the whole file when it is missing or unreadable, takes
 *                 its default: no RAD path, no game, and letterbox on.
 */
void ev_settings_load(ev_settings *settings);

/**
 * Saves the settings, creating their folder and file if needed.
 *
 * @param settings The settings to save.
 * @return Whether they were written.
 */
bool ev_settings_save(const ev_settings *settings);
