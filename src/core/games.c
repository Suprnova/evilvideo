#include "games.h"

#include <stddef.h>

const ev_game ev_games[EV_GAME_COUNT] = {
    { L"n100f", L"Scooby-Doo! Night of 100 Frights", 640, 480 },
    { L"bfbb", L"SpongeBob SquarePants: Battle for Bikini Bottom", 640, 480 },
    { L"tssm", L"The SpongeBob SquarePants Movie", 512, 480 },
    { L"incredibles", L"The Incredibles", 512, 480 },
    { L"rotu", L"The Incredibles: Rise of the Underminer", 512, 480 },
};

const ev_game *ev_game_find(const wchar_t *id)
{
    for (size_t i = 0; i < EV_GAME_COUNT; i++) {
        if (_wcsicmp(ev_games[i].id, id) == 0)
            return &ev_games[i];
    }
    return NULL;
}
