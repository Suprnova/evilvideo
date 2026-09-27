#include "proc.h"
#include "test.h"

#include <shellapi.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define CHILD_ERRORS_LENGTH 100000
#define CHILD_EXIT_CODE 3

typedef struct child_run {
    bool ok;
    char lines[4][16];
    int line_count;
    char *errors;
    DWORD exit_code;
} child_run;

static bool quotes_as(const wchar_t *arg, const wchar_t *expected)
{
    ev_cmdline cmdline = { 0 };

    ev_cmdline_add(&cmdline, arg);

    return wcscmp(cmdline.text, expected) == 0;
}

void add_test_child(ev_cmdline *cmdline, const wchar_t *mode)
{
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    ev_cmdline_add(cmdline, self);
    ev_cmdline_add(cmdline, L"child");
    ev_cmdline_add(cmdline, mode);
}

static void collect_line(const char *line, void *context)
{
    child_run *run = context;
    if (run->line_count < 4)
        snprintf(run->lines[run->line_count], sizeof run->lines[0], "%s", line);
    run->line_count++;
}

static child_run run_child(void)
{
    ev_job job;
    ev_job_create(&job);
    ev_cmdline cmdline = { 0 };
    add_test_child(&cmdline, L"output");
    child_run run = { 0 };

    run.ok = ev_process_run(&job, &cmdline, collect_line, &run, &run.errors, &run.exit_code);

    ev_job_close(&job);
    return run;
}

int proc_test_child(const char *mode)
{
    if (strcmp(mode, "sleep") == 0)
        Sleep(INFINITE);

    // More than a pipe holds, written before any output, so a parent that drains only stdout would deadlock.
    static char errors[CHILD_ERRORS_LENGTH];
    memset(errors, 'e', sizeof errors);
    DWORD written;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), errors, sizeof errors, &written, NULL);

    static const char output[] = "one\r\ntwo\nthree";
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), output, sizeof output - 1, &written, NULL);
    return CHILD_EXIT_CODE;
}

static void add_leaves_plain_argument_unquoted(void)
{
    EXPECT(quotes_as(L"-hide_banner", L"-hide_banner"));
}

static void add_leaves_trailing_backslash_of_plain_argument(void)
{
    EXPECT(quotes_as(L"C:\\dir\\", L"C:\\dir\\"));
}

static void add_quotes_argument_with_spaces(void)
{
    EXPECT(quotes_as(L"C:\\my videos\\in.mp4", L"\"C:\\my videos\\in.mp4\""));
}

static void add_quotes_empty_argument(void)
{
    EXPECT(quotes_as(L"", L"\"\""));
}

static void add_doubles_trailing_backslashes_of_quoted_argument(void)
{
    EXPECT(quotes_as(L"C:\\my dir\\", L"\"C:\\my dir\\\\\""));
}

static void add_escapes_quotes(void)
{
    EXPECT(quotes_as(L"say \"hi\"", L"\"say \\\"hi\\\"\""));
}

static void add_doubles_backslashes_before_quote(void)
{
    EXPECT(quotes_as(L"a\\\"b", L"\"a\\\\\\\"b\""));
}

static void add_separates_arguments_with_spaces(void)
{
    ev_cmdline cmdline = { 0 };

    ev_cmdline_add(&cmdline, L"-i");
    ev_cmdline_add(&cmdline, L"in put.mp4");

    EXPECT(wcscmp(cmdline.text, L"-i \"in put.mp4\"") == 0);
}

static void add_round_trips_through_command_line_to_argv(void)
{
    static const wchar_t *const args[] = {
        L"plain", L"", L"two words", L"tab\there", L"C:\\dir\\", L"C:\\my dir\\", L"\"", L"\\\"",
        L"a\\\\\"b", L"f??????.jpg*1-150", L"/F29.97", L"trailing\\\\",
    };
    const int count = (int)(sizeof args / sizeof args[0]);
    ev_cmdline cmdline = { 0 };
    ev_cmdline_add(&cmdline, L"program.exe");
    for (int i = 0; i < count; i++)
        ev_cmdline_add(&cmdline, args[i]);

    int argc;
    wchar_t **argv = CommandLineToArgvW(cmdline.text, &argc);

    EXPECT(argv != NULL && argc == count + 1);
    for (int i = 0; argv && i < count && i + 1 < argc; i++)
        EXPECT(wcscmp(argv[i + 1], args[i]) == 0);
    LocalFree(argv);
}

static void add_drops_argument_that_does_not_fit(void)
{
    wchar_t *huge = malloc(EV_CMDLINE_MAX * sizeof *huge);
    wmemset(huge, L'x', EV_CMDLINE_MAX - 1);
    huge[EV_CMDLINE_MAX - 1] = L'\0';
    ev_cmdline cmdline = { 0 };
    ev_cmdline_add(&cmdline, L"first");

    ev_cmdline_add(&cmdline, huge);
    ev_cmdline_add(&cmdline, L"later");

    EXPECT(cmdline.too_long);
    EXPECT(wcscmp(cmdline.text, L"first") == 0);
    free(huge);
}

static void start_refuses_command_line_that_is_too_long(void)
{
    ev_job job;
    ev_job_create(&job);
    ev_cmdline cmdline = { .too_long = true };
    HANDLE process;

    bool started = ev_process_start(&job, &cmdline, false, &process);

    EXPECT(!started && GetLastError() == ERROR_FILENAME_EXCED_RANGE);
    ev_job_close(&job);
}

static void cancelling_job_ends_its_processes(void)
{
    ev_job job;
    ev_job_create(&job);
    ev_cmdline cmdline = { 0 };
    add_test_child(&cmdline, L"sleep");
    HANDLE process;
    bool started = ev_process_start(&job, &cmdline, false, &process);

    ev_job_cancel(&job);

    DWORD exit_code = 0;
    EXPECT(started && WaitForSingleObject(process, 5000) == WAIT_OBJECT_0);
    EXPECT(started && GetExitCodeProcess(process, &exit_code) && exit_code == ERROR_CANCELLED);
    EXPECT(ev_job_cancelled(&job));
    if (started)
        CloseHandle(process);
    ev_job_close(&job);
}

static void cancelled_job_refuses_new_processes(void)
{
    ev_job job;
    ev_job_create(&job);
    ev_job_cancel(&job);
    ev_cmdline cmdline = { 0 };
    add_test_child(&cmdline, L"sleep");
    HANDLE process;

    bool started = ev_process_start(&job, &cmdline, false, &process);

    EXPECT(!started && GetLastError() == ERROR_CANCELLED);
    if (started) {
        TerminateProcess(process, 1);
        CloseHandle(process);
    }
    ev_job_close(&job);
}

static void closing_job_ends_its_processes(void)
{
    ev_job job;
    ev_job_create(&job);
    ev_cmdline cmdline = { 0 };
    add_test_child(&cmdline, L"sleep");
    HANDLE process;
    bool started = ev_process_start(&job, &cmdline, false, &process);

    ev_job_close(&job);

    EXPECT(started && WaitForSingleObject(process, 5000) == WAIT_OBJECT_0);
    if (started)
        CloseHandle(process);
}

static void run_passes_each_output_line(void)
{
    child_run run = run_child();

    EXPECT(run.ok);
    EXPECT(run.line_count == 3);
    EXPECT(strcmp(run.lines[0], "one") == 0);
    EXPECT(strcmp(run.lines[1], "two") == 0);
    EXPECT(strcmp(run.lines[2], "three") == 0);
    free(run.errors);
}

static void run_collects_all_errors(void)
{
    child_run run = run_child();

    EXPECT(run.errors != NULL && strlen(run.errors) == CHILD_ERRORS_LENGTH && run.errors[0] == 'e');
    free(run.errors);
}

static void run_returns_exit_code(void)
{
    child_run run = run_child();

    EXPECT(run.exit_code == CHILD_EXIT_CODE);
    free(run.errors);
}

void proc_tests(void)
{
    RUN_TEST(add_leaves_plain_argument_unquoted);
    RUN_TEST(add_leaves_trailing_backslash_of_plain_argument);
    RUN_TEST(add_quotes_argument_with_spaces);
    RUN_TEST(add_quotes_empty_argument);
    RUN_TEST(add_doubles_trailing_backslashes_of_quoted_argument);
    RUN_TEST(add_escapes_quotes);
    RUN_TEST(add_doubles_backslashes_before_quote);
    RUN_TEST(add_separates_arguments_with_spaces);
    RUN_TEST(add_round_trips_through_command_line_to_argv);
    RUN_TEST(add_drops_argument_that_does_not_fit);
    RUN_TEST(start_refuses_command_line_that_is_too_long);
    RUN_TEST(cancelling_job_ends_its_processes);
    RUN_TEST(cancelled_job_refuses_new_processes);
    RUN_TEST(closing_job_ends_its_processes);
    RUN_TEST(run_passes_each_output_line);
    RUN_TEST(run_collects_all_errors);
    RUN_TEST(run_returns_exit_code);
}
