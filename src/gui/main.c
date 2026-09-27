#include <windows.h>

#include "resource.h"

static INT_PTR CALLBACK main_dialog_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    (void)wparam;
    (void)lparam;

    switch (message) {
    case WM_INITDIALOG:
        return TRUE;
    case WM_CLOSE:
        EndDialog(dialog, 0);
        return TRUE;
    }
    return FALSE;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line, int show)
{
    (void)previous;
    (void)command_line;
    (void)show;

    return (int)DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_MAIN), NULL, main_dialog_proc, 0);
}
