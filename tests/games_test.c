#include "games.h"
#include "test.h"

static void find_returns_each_game_by_id(void)
{
    for (size_t i = 0; i < EV_GAME_COUNT; i++) {
        const ev_game *expected = &ev_games[i];

        const ev_game *game = ev_game_find(expected->id);

        EXPECT(game == expected);
    }
}

static void find_ignores_case(void)
{
    const ev_game *expected = ev_game_find(L"incredibles");

    EXPECT(ev_game_find(L"InCREDibles") == expected);
}

static void find_returns_null_for_unknown_id(void)
{
    EXPECT(ev_game_find(L"rat") == NULL);
}

static void find_returns_null_for_empty_id(void)
{
    EXPECT(ev_game_find(L"") == NULL);
}

static void output_size_matches_framebuffer(void)
{
    static const struct {
        const wchar_t *id;
        int width;
        int height;
    } expected[] = {
        { L"n100f", 640, 480 },
        { L"bfbb", 640, 480 },
        { L"tssm", 512, 480 },
        { L"incredibles", 512, 480 },
        { L"rotu", 512, 480 },
    };

    for (size_t i = 0; i < sizeof expected / sizeof expected[0]; i++) {
        const ev_game *game = ev_game_find(expected[i].id);

        EXPECT(game != NULL && game->width == expected[i].width && game->height == expected[i].height);
    }
}

void games_tests(void)
{
    RUN_TEST(find_returns_each_game_by_id);
    RUN_TEST(find_ignores_case);
    RUN_TEST(find_returns_null_for_unknown_id);
    RUN_TEST(find_returns_null_for_empty_id);
    RUN_TEST(output_size_matches_framebuffer);
}
