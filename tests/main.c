#include "test.h"

#include <string.h>

bool test_failed;
static int failures;

void run_test(const char *name, void (*test)(void))
{
    test_failed = false;
    test();
    printf("%s %s\n", test_failed ? "FAIL" : "pass", name);
    if (test_failed)
        failures++;
}

void cli_tests(void);
void games_tests(void);
void ffmpeg_tests(void);
void files_tests(void);
void rad_tests(void);
int rad_test_fake(void);
void convert_tests(void);
int convert_test_fake_ffmpeg(void);
void proc_tests(void);
void settings_tests(void);
int proc_test_child(const char *mode);

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "child") == 0)
        return proc_test_child(argv[2]);
    if (argc > 1 && (strcmp(argv[1], "Binkc") == 0 || strcmp(argv[1], "BinkMix") == 0))
        return rad_test_fake();
    if (argc > 1 && strcmp(argv[1], "-hide_banner") == 0)
        return convert_test_fake_ffmpeg();

    games_tests();
    proc_tests();
    ffmpeg_tests();
    files_tests();
    rad_tests();
    convert_tests();
    settings_tests();
    cli_tests();

    printf("%d failed\n", failures);
    return failures == 0 ? 0 : 1;
}
