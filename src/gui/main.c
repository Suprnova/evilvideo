#define COBJMACROS

#include "convert.h"
#include "ffmpeg.h"
#include "files.h"
#include "resource.h"
#include "settings.h"
#include "version.h"

#include <windows.h>
#include <commctrl.h>
#include <math.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#define WM_APP_PROGRESS (WM_APP + 1)
#define WM_APP_DONE (WM_APP + 2)

// The longest input or output path the dialog accepts, in characters.
#define PATH_SIZE 1024

static const wchar_t title[] = L"evilvideo";
static const wchar_t *const stage_names[] = { L"Reading", L"Preparing", L"Compressing", L"Mixing audio", L"Saving" };
static const int input_controls[] = { IDC_INPUT,  IDC_INPUT_BROWSE,  IDC_GAME,      IDC_LETTERBOX, IDC_TRIM,
                                      IDC_OUTPUT, IDC_OUTPUT_BROWSE, IDC_RAD_LOCATE };

static const COMDLG_FILTERSPEC video_types[] = {
    { L"Videos", L"*.mp4;*.m4v;*.mov;*.3gp;*.mkv;*.webm;*.avi;*.wmv;*.asf;*.flv;*.mpg;*.mpeg;*.vob;*.ts;*.m2ts;*.mts;"
                 L"*.ogv;*.ogg" },
    { L"All files", L"*.*" },
};
static const COMDLG_FILTERSPEC bink_types[] = { { L"Bink videos", L"*.bik" } };
static const COMDLG_FILTERSPEC rad_types[] = { { L"radvideo64.exe", L"radvideo64.exe" }, { L"Programs", L"*.exe" } };

static struct {
    HWND dialog;
    bool ffmpeg_found;
    wchar_t ffmpeg[MAX_PATH];
    bool rad_found;
    wchar_t rad[MAX_PATH];
    ev_settings settings;
    // Whether the user typed or picked the output, which then no longer follows the input.
    bool output_edited;
    bool filling_output;
    // The conversion in progress, whose strings the worker thread reads until it posts WM_APP_DONE.
    HANDLE thread;
    ev_job job;
    ev_conversion conversion;
    wchar_t input[PATH_SIZE];
    wchar_t output[PATH_SIZE];
    bool closing;
} app;

static const wchar_t *file_name(const wchar_t *path)
{
    const wchar_t *name = path;
    for (const wchar_t *c = path; *c; c++) {
        if (*c == L'\\' || *c == L'/' || *c == L':')
            name = c + 1;
    }
    return name;
}

static HWND control(int id)
{
    return GetDlgItem(app.dialog, id);
}

static void set_status(const wchar_t *format, ...)
{
    wchar_t text[2048];
    va_list args;
    va_start(args, format);
    vswprintf(text, ARRAYSIZE(text), format, args);
    va_end(args);
    SetDlgItemTextW(app.dialog, IDC_STATUS, text);
}

static void alert(UINT icon, const wchar_t *text)
{
    MessageBoxW(app.dialog, text, title, MB_OK | icon);
}

static bool reject(int id, const wchar_t *problem)
{
    alert(MB_ICONWARNING, problem);
    SetFocus(control(id));
    return false;
}

// Flashes the taskbar button until the window comes to the foreground, if it is in the background.
static void flash(void)
{
    FLASHWINFO info = { sizeof info, app.dialog, FLASHW_ALL | FLASHW_TIMERNOFG, 0, 0 };
    FlashWindowEx(&info);
}

// Changing the style resets the bar, so it only changes when the mode does.
static void set_marquee(bool on)
{
    HWND bar = control(IDC_PROGRESS);
    LONG_PTR style = GetWindowLongPtrW(bar, GWL_STYLE);
    if (on == ((style & PBS_MARQUEE) != 0))
        return;

    SetWindowLongPtrW(bar, GWL_STYLE, on ? style | PBS_MARQUEE : style & ~PBS_MARQUEE);
    SendMessageW(bar, PBM_SETMARQUEE, on, 0);
}

static void set_progress(int percent)
{
    set_marquee(false);
    SendDlgItemMessageW(app.dialog, IDC_PROGRESS, PBM_SETPOS, percent, 0);
}

static void update_trim(void)
{
    bool enabled = !app.thread && IsDlgButtonChecked(app.dialog, IDC_TRIM) == BST_CHECKED;
    EnableWindow(control(IDC_TRIM_START), enabled);
    EnableWindow(control(IDC_TRIM_END), enabled);
}

static void update_rad(void)
{
    SetDlgItemTextW(app.dialog, IDC_RAD_STATUS,
                    app.rad_found ? L"RAD Video Tools: found" : L"RAD Video Tools: not found");
    ShowWindow(control(IDC_RAD_LINK), app.rad_found ? SW_HIDE : SW_SHOW);
}

static void set_running(bool running)
{
    for (size_t i = 0; i < ARRAYSIZE(input_controls); i++)
        EnableWindow(control(input_controls[i]), !running);
    update_trim();
    SetDlgItemTextW(app.dialog, IDOK, running ? L"Cancel" : L"Convert");
    EnableWindow(control(IDOK), true);
}

static void set_input(const wchar_t *path)
{
    SetDlgItemTextW(app.dialog, IDC_INPUT, path);
}

// Makes the output follow the input as <input without extension>.bik, until the user edits the output.
static void fill_output(void)
{
    if (app.output_edited)
        return;

    wchar_t input[PATH_SIZE], output[PATH_SIZE + 4] = L"";
    GetDlgItemTextW(app.dialog, IDC_INPUT, input, PATH_SIZE);
    if (input[0]) {
        const wchar_t *extension = wcsrchr(file_name(input), L'.');
        int stem = (int)(extension ? extension - input : (ptrdiff_t)wcslen(input));
        swprintf(output, ARRAYSIZE(output), L"%.*ls.bik", stem, input);
    }
    app.filling_output = true;
    SetDlgItemTextW(app.dialog, IDC_OUTPUT, output);
    app.filling_output = false;
}

static void set_picker_folder(IFileDialog *picker, const wchar_t *path)
{
    wchar_t folder[PATH_SIZE];
    int length = (int)(file_name(path) - path);
    if (length == 0 || length >= PATH_SIZE)
        return;

    swprintf(folder, PATH_SIZE, L"%.*ls", length, path);
    IShellItem *item;
    if (SUCCEEDED(SHCreateItemFromParsingName(folder, NULL, &IID_IShellItem, (void **)&item))) {
        IFileDialog_SetFolder(picker, item);
        IShellItem_Release(item);
    }
}

/*
 * Shows a file picker.
 *
 * save: whether to pick a file to write rather than one to open. The overwrite prompt is left to Convert, which asks
 *       whatever way the output was chosen.
 * current: the path the picker starts from, or an empty string.
 * path: receives the picked path; holds PATH_SIZE characters.
 */
static bool pick_file(bool save, const COMDLG_FILTERSPEC *types, UINT type_count, const wchar_t *current,
                      wchar_t path[PATH_SIZE])
{
    IFileDialog *picker;
    if (FAILED(CoCreateInstance(save ? &CLSID_FileSaveDialog : &CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                &IID_IFileDialog, (void **)&picker)))
        return false;

    FILEOPENDIALOGOPTIONS options;
    IFileDialog_GetOptions(picker, &options);
    IFileDialog_SetOptions(picker, (options & ~FOS_OVERWRITEPROMPT) | FOS_FORCEFILESYSTEM);
    IFileDialog_SetFileTypes(picker, type_count, types);
    set_picker_folder(picker, current);
    if (save) {
        IFileDialog_SetDefaultExtension(picker, L"bik");
        IFileDialog_SetFileName(picker, file_name(current));
    }

    bool picked = false;
    IShellItem *item;
    if (SUCCEEDED(IFileDialog_Show(picker, app.dialog)) && SUCCEEDED(IFileDialog_GetResult(picker, &item))) {
        wchar_t *name;
        if (SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &name))) {
            picked = wcslen(name) < PATH_SIZE;
            if (picked)
                wcscpy(path, name);
            CoTaskMemFree(name);
        }
        IShellItem_Release(item);
    }
    IFileDialog_Release(picker);
    return picked;
}

static void browse_input(void)
{
    wchar_t current[PATH_SIZE], path[PATH_SIZE];
    GetDlgItemTextW(app.dialog, IDC_INPUT, current, PATH_SIZE);
    if (pick_file(false, video_types, ARRAYSIZE(video_types), current, path))
        set_input(path);
}

static void browse_output(void)
{
    wchar_t current[PATH_SIZE], path[PATH_SIZE];
    GetDlgItemTextW(app.dialog, IDC_OUTPUT, current, PATH_SIZE);
    if (pick_file(true, bink_types, ARRAYSIZE(bink_types), current, path))
        SetDlgItemTextW(app.dialog, IDC_OUTPUT, path);
}

static void locate_rad(void)
{
    wchar_t path[PATH_SIZE];
    if (!pick_file(false, rad_types, ARRAYSIZE(rad_types), app.rad_found ? app.rad : L"", path))
        return;

    wchar_t rad[MAX_PATH];
    if (!ev_rad_locate(path, NULL, rad)) {
        wchar_t problem[PATH_SIZE + 128];
        swprintf(problem, ARRAYSIZE(problem),
                 L"%ls is not RAD Video Tools. Choose radvideo64.exe, in the folder RAD Video Tools are installed in.",
                 file_name(path));
        alert(MB_ICONWARNING, problem);
        return;
    }
    wcscpy(app.rad, rad);
    wcscpy(app.settings.rad, rad);
    app.rad_found = true;
    update_rad();
}

static void show_in_folder(void)
{
    wchar_t parameters[PATH_SIZE + 16];
    swprintf(parameters, ARRAYSIZE(parameters), L"/select,\"%ls\"", app.output);
    ShellExecuteW(app.dialog, NULL, L"explorer.exe", parameters, NULL, SW_SHOWNORMAL);
}

static bool read_seconds(int id, double *seconds)
{
    wchar_t text[32], *end;
    GetDlgItemTextW(app.dialog, id, text, ARRAYSIZE(text));
    *seconds = wcstod(text, &end);
    return end != text && *end == L'\0' && isfinite(*seconds);
}

static bool is_folder(const wchar_t *path)
{
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && attributes & FILE_ATTRIBUTE_DIRECTORY;
}

// Checks the output, and stores its full path in app.output.
static bool check_output(void)
{
    wchar_t output[PATH_SIZE], *name = NULL;
    GetDlgItemTextW(app.dialog, IDC_OUTPUT, output, PATH_SIZE);
    DWORD length = output[0] ? GetFullPathNameW(output, PATH_SIZE, app.output, &name) : 0;
    if (length == 0 || length >= PATH_SIZE || !name)
        return reject(IDC_OUTPUT, L"Choose where to save the .bik.");

    wchar_t folder[PATH_SIZE], problem[PATH_SIZE + 32];
    swprintf(folder, PATH_SIZE, L"%.*ls", (int)(name - app.output), app.output);
    if (!is_folder(folder)) {
        swprintf(problem, ARRAYSIZE(problem), L"The folder %ls does not exist.", folder);
        return reject(IDC_OUTPUT, problem);
    }
    if (is_folder(app.output))
        return reject(IDC_OUTPUT, L"The output is a folder; add a file name.");
    return true;
}

// Checks the dialog's choices and fills in app.conversion from them.
static bool prepare_conversion(void)
{
    GetDlgItemTextW(app.dialog, IDC_INPUT, app.input, PATH_SIZE);
    DWORD attributes = GetFileAttributesW(app.input);
    if (!app.input[0])
        return reject(IDC_INPUT, L"Choose a video to convert.");
    if (attributes == INVALID_FILE_ATTRIBUTES || attributes & FILE_ATTRIBUTE_DIRECTORY)
        return reject(IDC_INPUT, L"The video does not exist.");

    int game = (int)SendDlgItemMessageW(app.dialog, IDC_GAME, CB_GETCURSEL, 0, 0);
    if (game == CB_ERR)
        return reject(IDC_GAME, L"Choose the game to convert for.");

    bool trim = IsDlgButtonChecked(app.dialog, IDC_TRIM) == BST_CHECKED;
    double start = 0, end = 0;
    if (trim && !(read_seconds(IDC_TRIM_START, &start) && read_seconds(IDC_TRIM_END, &end) && start >= 0 &&
                  start < end))
        return reject(IDC_TRIM_START, L"The trim needs a start of 0 s or more, and an end after the start.");

    if (!check_output())
        return false;
    if (!app.ffmpeg_found)
        return reject(IDOK, L"ffmpeg.exe is missing from evilvideo's folder; reinstall evilvideo.");
    if (!app.rad_found)
        return reject(IDC_RAD_LOCATE, L"RAD Video Tools are not installed. Install them, or use Locate… to find "
                                      L"radvideo64.exe.");

    if (GetFileAttributesW(app.output) != INVALID_FILE_ATTRIBUTES) {
        wchar_t question[PATH_SIZE + 64];
        swprintf(question, ARRAYSIZE(question), L"%ls already exists. Replace it?", file_name(app.output));
        if (MessageBoxW(app.dialog, question, title, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
            return false;
    }

    app.conversion = (ev_conversion){
        .input = app.input,
        .output = app.output,
        .game = &ev_games[game],
        .letterbox = IsDlgButtonChecked(app.dialog, IDC_LETTERBOX) == BST_CHECKED,
        .trim = trim,
        .trim_start = start,
        .trim_end = end,
        .ffmpeg = app.ffmpeg,
        .rad = app.rad,
        .context = app.dialog,
    };
    return true;
}

static void on_progress(ev_stage stage, int percent, void *context)
{
    PostMessageW(context, WM_APP_PROGRESS, stage, percent);
}

static DWORD WINAPI convert_thread(void *parameter)
{
    (void)parameter;
    wchar_t message[2048];
    ev_result result = ev_convert(&app.conversion, &app.job, message, ARRAYSIZE(message));
    PostMessageW(app.dialog, WM_APP_DONE, result, (LPARAM)_wcsdup(message));
    return 0;
}

static void start(void)
{
    if (!prepare_conversion())
        return;

    app.conversion.on_progress = on_progress;
    app.settings.game = app.conversion.game;
    app.settings.letterbox = app.conversion.letterbox;
    ev_settings_save(&app.settings);

    wchar_t message[512];
    if (!ev_job_create(&app.job)) {
        ev_error_message(message, ARRAYSIZE(message), L"Could not create a job object", GetLastError());
        alert(MB_ICONERROR, message);
        return;
    }
    app.thread = CreateThread(NULL, 0, convert_thread, NULL, 0, NULL);
    if (!app.thread) {
        ev_error_message(message, ARRAYSIZE(message), L"Could not start the conversion", GetLastError());
        alert(MB_ICONERROR, message);
        ev_job_close(&app.job);
        return;
    }

    ShowWindow(control(IDC_SHOW_FOLDER), SW_HIDE);
    set_progress(0);
    set_status(L"%ls…", stage_names[EV_STAGE_PROBE]);
    set_running(true);
}

static void cancel(void)
{
    ev_job_cancel(&app.job);
    EnableWindow(control(IDOK), false);
    set_status(L"Cancelling…");
}

static void show_progress(ev_stage stage, int percent)
{
    if (ev_job_cancelled(&app.job))
        return;

    if (percent < 0) {
        set_marquee(true);
        set_status(L"%ls…", stage_names[stage]);
    } else {
        set_progress(percent);
        set_status(L"%ls… %d%%", stage_names[stage], percent);
    }
}

static void finish(ev_result result, wchar_t *message)
{
    WaitForSingleObject(app.thread, INFINITE);
    CloseHandle(app.thread);
    app.thread = NULL;
    ev_job_close(&app.job);
    set_running(false);

    if (app.closing) {
        EndDialog(app.dialog, 0);
    } else if (result == EV_RESULT_CONVERTED) {
        set_progress(100);
        set_status(L"Saved %ls", file_name(app.output));
        ShowWindow(control(IDC_SHOW_FOLDER), SW_SHOW);
        flash();
    } else if (result == EV_RESULT_CANCELLED) {
        set_progress(0);
        set_status(L"Cancelled.");
    } else {
        const wchar_t *problem = message ? message : L"The conversion failed.";
        set_progress(0);
        set_status(L"Failed: %ls", problem);
        flash();
        alert(MB_ICONERROR, problem);
    }
    free(message);
}

static void close_window(void)
{
    if (!app.thread) {
        EndDialog(app.dialog, 0);
        return;
    }
    if (MessageBoxW(app.dialog, L"A conversion is running. Cancel it and quit?", title,
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
        app.closing = true;
        cancel();
    }
}

static void drop_file(HDROP drop)
{
    wchar_t path[PATH_SIZE];
    if (!app.thread && DragQueryFileW(drop, 0, path, PATH_SIZE))
        set_input(path);
    DragFinish(drop);
}

static void init(HWND dialog, const wchar_t *initial_input)
{
    app.dialog = dialog;
    SetDlgItemTextW(dialog, IDC_ABOUT, L"evilvideo " EV_VERSION_STRING
                                       L"\nThis software uses libraries from the FFmpeg project under the LGPLv3.");

    ev_settings_load(&app.settings);
    for (int i = 0; i < EV_GAME_COUNT; i++)
        SendDlgItemMessageW(dialog, IDC_GAME, CB_ADDSTRING, 0, (LPARAM)ev_games[i].name);
    if (app.settings.game)
        SendDlgItemMessageW(dialog, IDC_GAME, CB_SETCURSEL, app.settings.game - ev_games, 0);
    CheckDlgButton(dialog, IDC_LETTERBOX, app.settings.letterbox ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemTextW(dialog, IDC_TRIM_START, L"0");

    app.rad_found = ev_rad_locate(NULL, app.settings.rad, app.rad);
    update_rad();
    app.ffmpeg_found = ev_ffmpeg_locate(app.ffmpeg);
    if (!app.ffmpeg_found)
        set_status(L"ffmpeg.exe is missing from evilvideo's folder; reinstall evilvideo.");

    DragAcceptFiles(dialog, TRUE);
    if (initial_input)
        set_input(initial_input);
}

static void command(int id, int code)
{
    switch (id) {
    case IDC_INPUT:
        if (code == EN_CHANGE)
            fill_output();
        break;
    case IDC_OUTPUT:
        if (code == EN_CHANGE && !app.filling_output)
            app.output_edited = GetWindowTextLengthW(control(IDC_OUTPUT)) > 0;
        break;
    case IDC_INPUT_BROWSE:
        browse_input();
        break;
    case IDC_OUTPUT_BROWSE:
        browse_output();
        break;
    case IDC_TRIM:
        update_trim();
        break;
    case IDC_RAD_LOCATE:
        locate_rad();
        break;
    case IDC_SHOW_FOLDER:
        show_in_folder();
        break;
    case IDOK:
        if (app.thread)
            cancel();
        else
            start();
        break;
    case IDCANCEL:
        close_window();
        break;
    }
}

static INT_PTR CALLBACK main_dialog_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_INITDIALOG:
        init(dialog, (const wchar_t *)lparam);
        return TRUE;
    case WM_COMMAND:
        command(LOWORD(wparam), HIWORD(wparam));
        return TRUE;
    case WM_NOTIFY: {
        const NMHDR *header = (const NMHDR *)lparam;
        if (header->idFrom == IDC_RAD_LINK && (header->code == NM_CLICK || header->code == NM_RETURN))
            ShellExecuteW(dialog, NULL, EV_RAD_DOWNLOAD_URL, NULL, NULL, SW_SHOWNORMAL);
        return TRUE;
    }
    case WM_DROPFILES:
        drop_file((HDROP)wparam);
        return TRUE;
    case WM_APP_PROGRESS:
        show_progress((ev_stage)wparam, (int)lparam);
        return TRUE;
    case WM_APP_DONE:
        finish((ev_result)wparam, (wchar_t *)lparam);
        return TRUE;
    case WM_CLOSE:
        close_window();
        return TRUE;
    }
    return FALSE;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line, int show)
{
    (void)previous;
    (void)command_line;
    (void)show;

    INITCOMMONCONTROLSEX controls = { sizeof controls, ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS | ICC_LINK_CLASS };
    InitCommonControlsEx(&controls);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    ev_temp_cleanup();

    int argc;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const wchar_t *initial_input = argv && argc > 1 ? argv[1] : NULL;
    INT_PTR result = DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_MAIN), NULL, main_dialog_proc,
                                     (LPARAM)initial_input);
    LocalFree(argv);
    CoUninitialize();
    return (int)result;
}
