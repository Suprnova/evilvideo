#pragma once

#include <wchar.h>

/** Number of entries in ev_games. */
#define EV_GAME_COUNT 5

/** A target game and the Bink output it needs. */
typedef struct ev_game {
    /** The game's shorthand identifier, such as L"bfbb". */
    const wchar_t *id;
    /** The game's full title. */
    const wchar_t *name;
    /** Target output width in pixels. */
    int width;
    /** Target output height in pixels. */
    int height;
} ev_game;

/** Every supported game and its Bink output information. */
extern const ev_game ev_games[EV_GAME_COUNT];

/**
 * Finds a game by its ID, ignoring case.
 *
 * @param id The ID to look up, such as L"tssm".
 * @return The matching entry of ev_games, or NULL if no game has that ID.
 */
const ev_game *ev_game_find(const wchar_t *id);
